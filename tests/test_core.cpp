// Minimal self-contained tests for parsing, request building and the watch diff logic.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "AppInfo.h"
#include "api/ApiClient.h"
#include "api/Models.h"
#include "core/Database.h"
#include "core/DownloadStamp.h"
#include "core/HardwareId.h"
#include "core/Signature.h"
#include "core/Util.h"
#include "services/Exporter.h"
#include "services/AgentRunner.h"
#include "services/ReferralClient.h"
#include "services/Updater.h"
#include "services/WatchService.h"

using namespace su;

static int g_failures = 0;
#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
      ++g_failures;                                                       \
    }                                                                     \
  } while (0)

static void testParseStealerPage() {
  auto j = json::parse(R"({
    "items": [
      {"id":"a1","url":"https://site.com/login","username":null,"username_masked":"jo***@ex***.com",
       "password":null,"password_strength":3,"unlocked":false,"is_email":true,"added_at":"2026-10-02T13:45:00Z"},
      {"id":null,"url":null,"username":"john","password":"hunter2","password_strength":null,"unlocked":true,
       "added_at":null,"category":"employees"}
    ],
    "total": 128, "total_unlocked": 1, "page": 1, "page_size": 100, "blacklisted_value": null
  })");
  LeakPage p = parseLeakPage(j);
  CHECK(p.items.size() == 2);
  CHECK(p.total == 128);
  CHECK(!p.blacklisted);
  CHECK(p.items[0].username == "jo***@ex***.com");
  CHECK(p.items[0].strength && *p.items[0].strength == 3);
  CHECK(std::string(strengthLabel(p.items[0].strength)) == "Weak");
  CHECK(p.items[1].password == "hunter2");
  CHECK(p.items[1].info == "Employee");
  CHECK(leakKey(p.items[0]) == "id:a1");
  CHECK(leakKey(p.items[1]).rfind("c:", 0) == 0);
}

static void testAdvancedBody() {
  AdvQuery q;
  q[AdvField::UrlDomain].include = {"example.com", " example.org "};
  q[AdvField::UrlDomain].includeMatch = MatchType::EndsWith;
  q[AdvField::Password].exclude = {"123"};
  q[AdvField::UrlTld].include = {"fr"};
  q[AdvField::UrlPort].include = {"8080"};
  q[AdvField::UrlPort].exclude = {"80"};
  q[AdvField::PasswordHash].include = {"5baa6"};
  q[AdvField::PasswordHash].exclude = {"ffff"};  // include only: never sent
  q[AdvField::Username].include = {"   "};
  q.idKind = IdKind::Email;
  q.strength = 2;
  q.addedFrom = "2026-01-01";
  q.addedTo = "2026-13-01";  // invalid: ignored
  q.forceAnd = true;
  CHECK(q.valueCount() == 9 && q.hasFilters());
  json b = buildAdvancedBody(q);
  CHECK(b["url_domain"].size() == 2 && b["url_domain"][1] == "example.org");
  CHECK(b["url_domain_match_type"] == "ends_with");
  CHECK(b["password_not"][0] == "123");
  CHECK(!b.contains("password_not_match_type"));  // contains = default, omitted
  CHECK(b["url_tld"][0] == "fr" && !b.contains("url_tld_match_type"));
  CHECK(b["url_port"][0] == 8080 && b["url_port_not"][0] == 80);
  CHECK(b["password_hash"][0] == "5baa6" && !b.contains("password_hash_not"));
  CHECK(!b.contains("username"));
  CHECK(b["is_email"] == true);
  CHECK(b["password_strength"] == "weak");
  CHECK(b["added_from"] == "2026-01-01T00:00:00Z");
  CHECK(!b.contains("added_to"));
  CHECK(b["force_and"] == true);
  CHECK(buildAdvancedBody(AdvQuery{}).empty() && !AdvQuery{}.hasFilters());

  CHECK(advValueError(AdvField::Url, "ab") != "" && advValueError(AdvField::Url, "abc") == "");
  CHECK(advValueError(AdvField::UrlTld, "fr") == "" && advValueError(AdvField::UrlTld, "toolongtld1") != "");
  CHECK(advValueError(AdvField::UrlPort, "443") == "" && advValueError(AdvField::UrlPort, "80a") != "" &&
        advValueError(AdvField::UrlPort, "70000") != "");
  CHECK(advValueError(AdvField::UsernameHash, "5baa") == "" && advValueError(AdvField::UsernameHash, "5bxz") != "");
  CHECK(isIsoDate("2026-02-28") && !isIsoDate("2026-2-28") && !isIsoDate("2026-00-10"));
  CHECK(std::string(advFieldInfo(AdvField::EmailHost).key) == "email_host" && !advFieldInfo(AdvField::UsernameHash).excludeHint);
}

static void testUtil() {
  int m = 0;
  CHECK(parseHHMM("08:30", m) && m == 510);
  CHECK(!parseHHMM("24:00", m));
  CHECK(!parseHHMM("8", m));
  CHECK(formatHHMM(510) == "08:30");
  CHECK(splitList(" a, ,b ,c").size() == 3);
  CHECK(urlEncode("a b@c") == "a%20b%40c");
  CHECK(looksLikeEmail("x@y.z") && !looksLikeEmail("@y") && !looksLikeEmail("user"));
  CHECK(normalizeDomain("  https://Example.COM:8443/login?x=1 ") == "example.com");
  CHECK(normalizeDomain("mail.acme-corp.co.uk.") == "mail.acme-corp.co.uk");
  CHECK(looksLikeDomain("example.com") && looksLikeDomain("mail.acme-corp.co.uk"));
  CHECK(!looksLikeDomain("john@example.com") && !looksLikeDomain("jdoe_gamer") && !looksLikeDomain("john doe.com"));
  CHECK(!looksLikeDomain("1.2.3.4") && !looksLikeDomain("example.c") && !looksLikeDomain("-bad.com") && !looksLikeDomain("a..com"));
}

static void testDailyDue() {
  const int64_t now = nowEpoch();
  CHECK(WatchService::dailyDue(now, 0, 0));                  // never ran
  CHECK(WatchService::dailyDue(now, 0, now - 25 * 3600));    // missed a day
  CHECK(!WatchService::dailyDue(now, 0, now - 60));          // ran a minute ago
}

// Fake dataset: per source, the list of leak IDs currently "on the server".
struct FakeServer {
  std::map<int, std::vector<std::string>> ids;  // source -> ids
  bool fail = false;
  int calls = 0;

  Result<LeakPage> fetch(const WatchItem&, LeakSource s, int page) {
    ++calls;
    if (fail) return Result<LeakPage>::failure({503, "down"});
    const auto& all = ids[static_cast<int>(s)];
    LeakPage p;
    p.total = static_cast<int64_t>(all.size());
    p.page = page;
    p.pageSize = WatchService::kPageSize;
    for (size_t i = static_cast<size_t>(page - 1) * WatchService::kPageSize;
         i < all.size() && i < static_cast<size_t>(page) * WatchService::kPageSize; ++i) {
      LeakRow r;
      r.id = all[i];
      r.url = "https://site/" + all[i];
      p.items.push_back(r);
    }
    return Result<LeakPage>::success(p);
  }
};

static void testWatchDiff() {
  Database db;
  CHECK(db.open(":memory:"));
  FakeServer server;
  for (int i = 0; i < 150; ++i) server.ids[0].push_back("s" + std::to_string(i));  // 2 pages

  std::vector<std::string> notifications;
  WatchService watch(
      db, [&](const WatchItem& w, LeakSource s, int p) { return server.fetch(w, s, p); },
      [&](const std::string& t, const std::string& m) { notifications.push_back(t + ": " + m); }, nullptr);
  watch.pauseBetweenRequestsMs_ = 0;

  int64_t id = db.addWatch("john@example.com", WatchKind::Email);
  CHECK(id > 0);
  CHECK(db.addWatch("JOHN@example.com", WatchKind::Email) == 0);  // duplicate (case-insensitive)

  // 1) Baseline: no alert, everything recorded.
  watch.checkItem(id);
  WatchItem w;
  CHECK(db.getWatch(id, w));
  CHECK(w.baselineDone);
  CHECK(w.stealerTotal == 150);
  CHECK(w.comboTotal == kTotalUnavailable);
  CHECK(notifications.empty());
  CHECK(db.listAlerts().empty());
  CHECK(db.watchLeaks(id).size() == 150);

  // 2) Unchanged: one request (stealer logs only), no alert.
  server.calls = 0;
  watch.checkItem(id);
  CHECK(server.calls == 1);
  CHECK(notifications.empty());

  // 3) Two new leaks appear -> one alert with 2 leaks.
  server.ids[0].push_back("s_new1");
  server.ids[0].push_back("s_new2");
  watch.checkItem(id);
  CHECK(notifications.size() == 1);
  auto alerts = db.listAlerts();
  CHECK(alerts.size() == 1);
  CHECK(alerts[0].newCount == 2);
  CHECK(db.alertLeaks(alerts[0].id).size() == 2);
  CHECK(db.unreadAlertCount() == 1);
  db.markAllAlertsRead();
  CHECK(db.unreadAlertCount() == 0);

  // 4) Service down: no alert, status recorded, totals unchanged.
  server.fail = true;
  server.ids[0].push_back("s_new3");
  watch.checkItem(id);
  CHECK(db.getWatch(id, w));
  CHECK(!w.lastStatus.empty());
  CHECK(w.stealerTotal == 152);
  CHECK(notifications.size() == 1);

  // 5) Back up: the leak missed during the outage is reported.
  server.fail = false;
  watch.checkItem(id);
  CHECK(notifications.size() == 2);
  CHECK(db.getWatch(id, w) && w.lastStatus.empty());

  // 6) Removing the value removes its alerts and leaks.
  db.removeWatch(id);
  CHECK(db.listAlerts().empty());
  CHECK(db.watchLeaks(id).empty());
}

static void testDomainWatch() {
  Database db;
  CHECK(db.open(":memory:"));
  FakeServer server;
  server.ids[2] = {"d1", "d2"};
  std::vector<std::string> notifications;
  WatchService watch(
      db, [&](const WatchItem& w, LeakSource s, int p) { return server.fetch(w, s, p); },
      [&](const std::string& t, const std::string& m) { notifications.push_back(t + ": " + m); }, nullptr);
  watch.pauseBetweenRequestsMs_ = 0;
  CHECK(WatchService::sourcesFor(WatchKind::Domain).size() == 1);

  int64_t id = db.addWatch("acme-corp.com", WatchKind::Domain);
  watch.checkItem(id);
  WatchItem w;
  CHECK(db.getWatch(id, w) && w.baselineDone && w.stealerTotal == 2 && w.comboTotal == kTotalUnavailable);
  CHECK(server.calls == 1);  // only the domain dataset is queried
  server.ids[2].push_back("d3");
  watch.checkItem(id);
  CHECK(notifications.size() == 1);
  CHECK(notifications[0].find("Domain leaks") != std::string::npos);
}

static void testCsvAndFormatting() {
  CHECK(csvField("plain") == "plain");
  CHECK(csvField("a,b") == "\"a,b\"");
  CHECK(csvField("say \"hi\"") == "\"say \"\"hi\"\"\"");
  CHECK(csvField("=1+1") == "'=1+1");
  CHECK(csvField("-cmd,x") == "\"'-cmd,x\"");
  CHECK(csvLine({"a", "b"}) == "a,b\r\n");
  CHECK(formatThousands(831632049166) == "831,632,049,166");
  CHECK(formatThousands(999) == "999");
  CHECK(formatCompact(196400) == "196K");
  CHECK(formatCompact(5.8e9) == "5.8B");
  CHECK(formatBytes(static_cast<int64_t>(12.4 * 1024 * 1024 * 1024)) == "12.4 GB");
}

static void testNewParsers() {
  auto prof = parseProfile(json::parse(R"({"first_name":"Alex","last_name":"D","email":"a@b.c",
    "subscription_points":2000,"extra_points":353,"subscription_gb":1000,"extra_gb":null,
    "plan":{"name":"Enterprise","plan_name":"Enterprise","bypass_points":false}})"));
  CHECK(prof.points && *prof.points == 2353);
  CHECK(prof.quotaBytes && *prof.quotaBytes == 1000);
  CHECK(prof.planName == "Enterprise" && !prof.bypassPoints);

  auto stats = parseLiveStats(json::parse(R"({"raw_lines_total":5,"raw_lines_7d":2,"leaks_total":3,"leaks_7d":1,
    "dark_web_posts_total":9,"sources_active":1,"credentials_per_second_24h":1.5,"sparkline_24h":[],"synced_at":"x"})"));
  CHECK(stats.available && stats.rawLinesTotal == 5 && stats.linesPerSecond24h == 1.5);
  CHECK(!parseLiveStats(json::parse(R"({"message":"Live stats not available"})")).available);

  auto dw = parseDarkWebPage(json::parse(R"({"items":[{"id":"p1","title":"T","content":"C","source":"forum",
    "source_name":"Forum X","truncated":false,"censored":true}],"total":1,"page":1,"page_size":25})"));
  CHECK(dw.items.size() == 1 && dw.items[0].source == "Forum X" && dw.items[0].censored);
}

static void testVersion() {
  // The built-in version (from VERSION) is a proper major.minor.patch number.
  const std::string appVersion = APP_VERSION_STR;
  CHECK(std::count(appVersion.begin(), appVersion.end(), '.') == 2);
  CHECK(appVersion.find_first_not_of("0123456789.") == std::string::npos);
}

static void testUpdater() {
  CHECK(compareVersions("1.0.0", "1.0.0") == 0 && compareVersions("1.0", "1.0.0") == 0);
  CHECK(compareVersions("1.10.0", "1.9.2") == 1 && compareVersions("1.0.0", "1.0.1") == -1 && compareVersions("2", "1.9.9") == 1);
  CHECK(compareVersions("v1.2.1", "1.2.0") == 1);

  // URLs: https anywhere, plain http only on this PC; ports kept; no credentials.
  auto api = parseUpdateUrl("http://localhost:4000/api/app/latest");
  CHECK(api && !api->secure && api->host == "localhost" && api->port == 4000 && api->path == "/api/app/latest");
  auto https = parseUpdateUrl("https://StayUnexposed.com/api/app/latest");
  CHECK(https && https->secure && https->host == "stayunexposed.com" && https->port == 443);
  CHECK(parseUpdateUrl("http://127.0.0.1:4000/x").has_value());
  CHECK(!parseUpdateUrl("http://stayunexposed.com/api/app/latest"));  // http off this PC
  CHECK(!parseUpdateUrl("https://user@stayunexposed.com/x") && !parseUpdateUrl("ftp://localhost/x"));
  CHECK(!parseUpdateUrl("http://localhost:99999/x") && !parseUpdateUrl("http://localhost:/x"));

  // The reply of the update API.
  const auto reply = nlohmann::json::parse(R"({"latestVersion":"1.2.1",
      "downloadUrl":"http://localhost:4000/download/StayUnexposed-1.2.1.exe",
      "notes":"Small fixes and better update checks.","publishedAt":"2026-11-13T08:32:19.125Z"})");
  auto info = parseUpdateManifest(reply, *api);
  CHECK(info && info->version == "1.2.1" && info->sha256.empty() && info->notes == "Small fixes and better update checks.");
  CHECK(info && info->url == "http://localhost:4000/download/StayUnexposed-1.2.1.exe");
  CHECK(info && formatReleaseDate(info->publishedAt) == "Nov 13, 2026");
  CHECK(formatReleaseDate("") == "" && formatReleaseDate("soon") == "");

  auto with = [&](const char* key, nlohmann::json value) {
    nlohmann::json j = reply;
    j[key] = value;
    return parseUpdateManifest(j, *api);
  };
  CHECK(!with("downloadUrl", "http://localhost:5000/download/a.exe"));    // other port = other server
  CHECK(!with("downloadUrl", "https://evil.example/StayUnexposed.exe"));  // other host
  CHECK(!with("latestVersion", "1.2.x") && !with("latestVersion", "") && !with("latestVersion", 3));
  CHECK(!with("sha256", "abc"));                                          // a hash, when given, must be valid
  CHECK(with("sha256", std::string(64, 'A')) && with("sha256", std::string(64, 'A'))->sha256 == std::string(64, 'a'));
  CHECK(!parseUpdateManifest(nlohmann::json::array(), *api));

  // An installer on 127.0.0.1 while the API is on localhost: same server (this PC), started instead of swapped.
  const char* setupUrl = "http://127.0.0.1:4000/downloads/StayUnexposed-Setup-1.2.1.exe";
  auto setup = with("downloadUrl", setupUrl);
  CHECK(setup && isInstallerUrl(setup->url));
  CHECK(!isInstallerUrl("http://localhost:4000/download/StayUnexposed-1.3.1.exe"));
  CHECK(isInstallerUrl("https://stayunexposed.com/d/StayUnexposed.msi?x=1") && isInstallerUrl("http://localhost/Install.exe"));
  CHECK(!with("downloadUrl", "http://127.0.0.1:4001/downloads/StayUnexposed-Setup-1.2.1.exe"));
}

static void testReferrals() {
  CHECK(rewardGroups(0) == 0 && rewardGroups(9) == 0 && rewardGroups(10) == 1 && rewardGroups(25) == 2);
  CHECK(towardNextReward(7) == 7 && towardNextReward(20) == 0);
  CHECK(formatDollars(rewardDollars(25)) == "$2.00");
  auto st = parseReferralStatus(nlohmann::json::parse(
      R"({"referrals":[{"id":"SUAAAABBBBCD","joined_at":"2026-11-01T10:00:00Z"},"SUEEEEFFFFGH",{}]})"));
  CHECK(st.referrals.size() == 2 && st.referrals[0].joinedAt == "2026-11-01T10:00:00Z" &&
        st.referrals[1].id == "SUEEEEFFFFGH");
  CHECK(mockReferrals().mock && mockReferrals().referrals.size() == 7);
  // Checksum: a generated ID passes; every single-character change and neighbour swap is caught.
  const std::string good = hwid::withCheck("T4BM2KNJC9");
  CHECK(isReferralId(good) && hwid::checkId(good) == hwid::IdCheck::Ok);
  const std::string alphabet = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
  int caught = 0, changes = 0;
  for (size_t i = 2; i < good.size(); ++i) {
    for (char c : alphabet) {
      if (c == good[i]) continue;
      std::string bad = good;
      bad[i] = c;
      ++changes;
      caught += hwid::checkId(bad) == hwid::IdCheck::BadChecksum;
    }
  }
  CHECK(changes > 0 && caught == changes);
  int swaps = 0, swapsCaught = 0;
  for (size_t i = 2; i + 1 < good.size(); ++i) {
    if (good[i] == good[i + 1]) continue;
    std::string bad = good;
    std::swap(bad[i], bad[i + 1]);
    ++swaps;
    swapsCaught += hwid::checkId(bad) != hwid::IdCheck::Ok;
  }
  CHECK(swaps > 0 && swapsCaught >= swaps - 1);  // Luhn mod N misses only one rare symbol pair
  // Reading rules and formatting
  std::string lower = good;
  for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  CHECK(normalizeReferralId(" " + lower + " ") == good);
  std::string noDashes = good;
  noDashes.erase(std::remove(noDashes.begin(), noDashes.end(), '-'), noDashes.end());
  CHECK(normalizeReferralId(noDashes) == good);
  CHECK(normalizeReferralId("su-oil") == "SU011");  // O->0, I/L->1, dashes dropped
  CHECK(hwid::checkId("SUT4BM2KNJ") == hwid::IdCheck::BadFormat);  // too short
  CHECK(hwid::checkId("SUT4BM2KNJU99") == hwid::IdCheck::BadFormat);  // U is not in the alphabet
  CHECK(!isReferralId("BW" + good.substr(2)));
  CHECK(referralIdProblem(good, "SUZZZZZZZZZZZ") == "" && referralIdProblem(good, good) != "");
  CHECK(referralIdProblem("SU1234", "") != "");
  // This PC's ID and the sample referrals all carry a valid check character.
  CHECK(isReferralId(hwid::userId()));
  for (const auto& r : mockReferrals().referrals) CHECK(isReferralId(r.id));
  CHECK(parseReferralStatus(nlohmann::json::object()).referrals.empty());
}

static void testHardwareId() {
  CHECK(hwid::sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  CHECK(hwid::base32({0xFF, 0xFF}, 3) == "ZZZ");
  CHECK(hwid::base32({0x00, 0x00}, 3) == "000");

  hwid::Facts a{"S123", "ABCD0001", "00:11:22:33:44:55"};
  hwid::Facts b = a;
  b.mac = "00:11:22:33:44:56";
  const std::string idA = hwid::idFromFacts(a);
  CHECK(idA == hwid::idFromFacts(a));  // deterministic
  CHECK(idA != hwid::idFromFacts(b));  // hardware-dependent
  CHECK(idA.size() == 13 && idA.rfind("SU", 0) == 0 && idA.find('-') == std::string::npos);
  CHECK(idA.substr(2).find_first_of("ILOU") == std::string::npos);  // Crockford alphabet: no ambiguous letters

  const std::string& mine = hwid::userId();
  CHECK(mine.size() == 13 && mine == hwid::userId());
  std::string url = hwid::referralUrl(mine);
  CHECK(url.find("ref=" + mine) != std::string::npos);
}

// Optional: `su_tests --live` calls the real API (uses search quota).
static int liveCheck() {
  {
    Updater up;
    auto c = up.check();
    if (!c.ok)
      std::printf("update check: HTTP %d -> %s\n", c.error.status, c.error.message.c_str());
    else if (c.value) {
      std::printf("update check: v%s available (%s), released %s\n", c.value->version.c_str(), c.value->url.c_str(),
                  formatReleaseDate(c.value->publishedAt).c_str());
      auto d = up.download(*c.value);  // download only, nothing is installed
      if (d.ok) {
        std::error_code ec;
        std::printf("update download: OK, %llu bytes, %s\n", static_cast<unsigned long long>(std::filesystem::file_size(d.value, ec)),
                    isInstallerUrl(c.value->url) ? "installer" : "app exe");
        std::filesystem::remove(d.value, ec);
      } else {
        std::printf("update download: HTTP %d -> %s\n", d.error.status, d.error.message.c_str());
      }
    }
    else
      std::printf("update check: up to date (running v%s)\n", APP_VERSION_STR);
  }
  ApiClient api;
  if (!api.configured()) {
    std::printf("No API host configured in this build.\n");
    return 1;
  }
  std::printf("user id: %s\n", hwid::userId().c_str());
  auto u = api.usage();
  if (u.ok)
    std::printf("usage: OK  used=%lld limit=%lld\n", static_cast<long long>(u.value.used.value_or(-1)),
                static_cast<long long>(u.value.limit.value_or(-1)));
  else
    std::printf("usage: HTTP %d -> %s\n", u.error.status, u.error.message.c_str());
  auto p = api.profile();
  if (p.ok)
    std::printf("profile: OK  points=%lld bypass=%d\n", static_cast<long long>(p.value.points.value_or(-1)),
                p.value.bypassPoints ? 1 : 0);
  else
    std::printf("profile: HTTP %d -> %s\n", p.error.status, p.error.message.c_str());
  auto ls = api.liveStats();
  if (ls.ok)
    std::printf("live-stats: OK  available=%d lines=%lld\n", ls.value.available ? 1 : 0,
                static_cast<long long>(ls.value.rawLinesTotal));
  else
    std::printf("live-stats: HTTP %d -> %s\n", ls.error.status, ls.error.message.c_str());
  if (ls.ok) std::printf("sparkline points=%zu sources=%lld\n", ls.value.sparkline24h.size(),
                         static_cast<long long>(ls.value.sourcesActive));
  auto is = api.indexStats();
  if (is.ok) {
    auto show = [](const char* n, const DatasetStats& d) {
      std::printf("stats %s: present=%d total=%lld today=%lld week=%lld month=%lld weeks=%zu", n, d.present ? 1 : 0,
                  static_cast<long long>(d.total), static_cast<long long>(d.today),
                  static_cast<long long>(d.thisWeek), static_cast<long long>(d.thisMonth), d.perWeek.size());
      if (!d.perWeek.empty())
        std::printf(" first=%s:%lld last=%s:%lld", d.weeks.front().c_str(), static_cast<long long>(d.perWeek.front()),
                    d.weeks.back().c_str(), static_cast<long long>(d.perWeek.back()));
      std::printf("\n");
    };
    std::printf("stats: available=%d darkweb=%lld\n", is.value.available ? 1 : 0,
                static_cast<long long>(is.value.darkWebPosts));
    show("leaks", is.value.leaks);
    show("raw_lines", is.value.rawLines);
    show("combolists", is.value.combolists);
  } else {
    std::printf("stats: HTTP %d -> %s\n", is.error.status, is.error.message.c_str());
  }
  auto s =api.searchIdentity("test@example.com", IdKind::Email, {}, 1, 5);
  if (s.ok)
    std::printf("search: OK  total=%lld unlocked=%lld items=%zu\n", static_cast<long long>(s.value.total),
                static_cast<long long>(s.value.totalUnlocked), s.value.items.size());
  if (s.ok) {
    size_t withId = 0, withPassword = 0;
    for (const auto& r : s.value.items) {
      withId += !r.id.empty();
      withPassword += !r.password.empty();
    }
    std::printf("search rows: with id=%zu with password=%zu\n", withId, withPassword);
  }
  {
    AdvQuery q;
    q[AdvField::UrlDomain].include = {"github.com"};
    q[AdvField::EmailDomain].include = {"example.com"};
    q[AdvField::Username].exclude = {"test"};
    q[AdvField::UrlScheme].include = {"https"};
    q[AdvField::UrlPort].exclude = {"8080"};
    q.forceAnd = false;
    auto adv = api.searchAdvanced(q, 1, 5);
    if (adv.ok)
      std::printf("advanced: OK  total=%lld items=%zu\n", static_cast<long long>(adv.value.total), adv.value.items.size());
    else
      std::printf("advanced: HTTP %d -> %s\n", adv.error.status, adv.error.message.c_str());
  }
  auto dl = api.domainLeaks("example.com", DomainCategory::All, {}, IdKind::Both, 1, 5);
  if (dl.ok) {
    size_t withId = 0, withPassword = 0;
    for (const auto& r : dl.value.items) {
      withId += !r.id.empty();
      withPassword += !r.password.empty();
    }
    std::printf("domain rows: items=%zu with id=%zu with password=%zu\n", dl.value.items.size(), withId, withPassword);
  } else {
    std::printf("domain rows: HTTP %d -> %s\n", dl.error.status, dl.error.message.c_str());
  }
  if (!s.ok)
    std::printf("search: HTTP %d -> %s\n", s.error.status, s.error.message.c_str());
  auto d = api.domainSummary("example.com");
  if (d.ok)
    std::printf("domain: OK  employees=%lld customers=%lld third=%lld\n", static_cast<long long>(d.value.employees),
                static_cast<long long>(d.value.customers), static_cast<long long>(d.value.thirdParties));
  else
    std::printf("domain: HTTP %d -> %s\n", d.error.status, d.error.message.c_str());
  return u.ok && p.ok && s.ok ? 0 : 1;
}

static void testUpdateCheckPath() {
  CHECK(su::updateCheckPath("/api/app/latest", "1.3.7", "SUT4BM2KNJC9H", false) ==
        "/api/app/latest?version=1.3.7&id=SUT4BM2KNJC9H");
  CHECK(su::updateCheckPath("/latest?x=1", "1.3.7", "SUT4BM2KNJC9H", true) ==
        "/latest?x=1&version=1.3.7&id=SUT4BM2KNJC9H&updater=1");
}

static void testDownloadStamp() {
  using namespace su;
  const std::string id = hwid::withCheck("T4BM2KNJC9");  // a valid referral id
  CHECK(hwid::checkId(id) == hwid::IdCheck::Ok);
  const std::vector<unsigned char> exe = {'M', 'Z', 0, 1, 2, 3, 4, 5, 6, 7};

  auto stamped = downloadstamp::appendStamp(exe, id);
  CHECK(stamped.size() == exe.size() + id.size() + 4 + 8);
  CHECK(std::equal(exe.begin(), exe.end(), stamped.begin()));  // original bytes untouched -> exe still runs
  auto got = downloadstamp::parseStamp(stamped.data(), stamped.size());
  CHECK(got && *got == id);

  // a messy spelling (lower case, spaces) is normalised to the same id
  auto messy = downloadstamp::appendStamp(exe, "su-t4bm 2knj-c9" + std::string(1, id.back()));
  auto gotMessy = downloadstamp::parseStamp(messy.data(), messy.size());
  CHECK(gotMessy && *gotMessy == id);

  CHECK(!downloadstamp::parseStamp(exe.data(), exe.size()));  // no stamp
  auto badMagic = stamped;
  badMagic.back() ^= 0xFF;
  CHECK(!downloadstamp::parseStamp(badMagic.data(), badMagic.size()));
  auto badLen = stamped;
  badLen[badLen.size() - 9] = 0xFF;  // length byte -> points past the start
  CHECK(!downloadstamp::parseStamp(badLen.data(), badLen.size()));

  // an id that fails the format/checksum is not stamped at all
  CHECK(downloadstamp::appendStamp(exe, "SU-T4BM-2KNJ").size() == exe.size());  // too short
}

static void testSignature() {
  using su::signature::Signer;
  using su::signature::acceptableUpdateSigner;
  const Signer su{"Stay Unexposed", "AA"}, renewed{"Stay Unexposed", "BB"}, other{"Someone Else", "CC"};
  CHECK(acceptableUpdateSigner(std::nullopt, std::nullopt));  // unsigned app: any update (first signed release)
  CHECK(acceptableUpdateSigner(std::nullopt, su));
  CHECK(acceptableUpdateSigner(su, su));
  CHECK(acceptableUpdateSigner(su, renewed));                 // renewed certificate, same publisher
  CHECK(!acceptableUpdateSigner(su, std::nullopt));           // signed app never takes an unsigned update
  CHECK(!acceptableUpdateSigner(su, other));
  // Missing and unsigned files have no verified signer.
  CHECK(!su::signature::verifiedSigner(L"Z:\\no\\such\\file.exe"));
  CHECK(!su::signature::ownSigner());  // su_tests.exe itself is never signed
#pragma warning(suppress : 4996)
  const char* signedExe = std::getenv("SU_TEST_SIGNED_EXE");
  if (signedExe && *signedExe) {  // optional: a file signed with a certificate trusted on this PC
    auto s = su::signature::verifiedSigner(su::widen(signedExe));
    CHECK(s && !s->subject.empty() && s->thumbprint.size() == 40);
    if (s) std::printf("signed by: %s (%s)\n", s->subject.c_str(), s->thumbprint.c_str());
  }
}

int main(int argc, char** argv) {
  if (argc > 1 && std::string(argv[1]) == "--live") return liveCheck();
  // Tooling / cross-language check: print the inviter stamped into a file (see admin /download).
  // Live check: register THIS PC with the referral server and read back its invitees (uses the compiled
  // kReferralApi). Proves the registration chain end to end against the real server.
  if (argc > 1 && std::string(argv[1]) == "--run-agent") {
    su::AgentRunner agent;
    std::printf("configured : %s\n", agent.configured() ? "yes" : "NO (empty kAgentUrl)");
    const bool ok = agent.ensureRunning();
    std::printf("running    : %s\n", ok ? "yes" : "no");
    return ok ? 0 : 1;
  }
  if (argc > 1 && std::string(argv[1]) == "--register") {
    su::ReferralClient rc;
    const std::string id = su::hwid::userId();
    std::printf("referral id : %s\nconfigured  : %s\n", id.c_str(), rc.configured() ? "yes" : "NO (empty kReferralApi)");
    auto r = rc.registerSelf(id);
    std::printf("registerSelf: %s\n", r.ok ? "OK" : ("FAILED: " + r.error.message).c_str());
    auto f = rc.fetch(id);
    if (f.ok) std::printf("invitees    : %zu\n", f.value.referrals.size());
    else std::printf("invitees    : FAILED: %s\n", f.error.message.c_str());
    return r.ok ? 0 : 1;
  }
  if (argc > 2 && std::string(argv[1]) == "--read-stamp") {
    std::ifstream f(argv[2], std::ios::binary);
    const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    const auto id = su::downloadstamp::parseStamp(bytes.data(), bytes.size());
    std::printf("%s\n", id ? id->c_str() : "(no stamp)");
    return id ? 0 : 1;
  }
  testParseStealerPage();
  testAdvancedBody();
  testUtil();
  testDailyDue();
  testWatchDiff();
  testDomainWatch();
  testCsvAndFormatting();
  testNewParsers();
  testHardwareId();
  testReferrals();
  testVersion();
  testUpdater();
  testUpdateCheckPath();
  testDownloadStamp();
  testSignature();
  if (g_failures == 0) std::printf("All tests passed.\n");
  return g_failures == 0 ? 0 : 1;
}
