#include "api/Models.h"

#include <cctype>
#include <cstdio>

#include "core/Util.h"

namespace su {

namespace {

std::string str(const json& j, const char* key) {
  auto it = j.find(key);
  if (it == j.end() || !it->is_string()) return {};
  return it->get<std::string>();
}

std::optional<int64_t> optInt(const json& j, const char* key) {
  auto it = j.find(key);
  if (it == j.end() || !it->is_number()) return std::nullopt;
  return it->get<int64_t>();
}

bool boolean(const json& j, const char* key) {
  auto it = j.find(key);
  return it != j.end() && it->is_boolean() && it->get<bool>();
}

std::string idString(const json& j) {
  auto it = j.find("id");
  if (it == j.end() || it->is_null()) return {};
  if (it->is_string()) return it->get<std::string>();
  return it->dump();  // numeric IDs
}

std::string categoryLabel(const std::string& c) {
  if (c == "employees" || c == "employee") return "Employee";
  if (c == "customers" || c == "customer") return "Customer";
  if (c == "third_parties" || c == "third_party") return "Third party";
  return c;
}

}  // namespace

const AdvFieldInfo& advFieldInfo(AdvField f) {
  static const AdvFieldInfo infos[] = {
      {"url", "URL", "Full URL of the page where the credentials were used.", "e.g. /admin, login.php, ?id=",
       "e.g. /test, staging, localhost", true, false, 3, 100},
      {"url_host", "URL host", "Host name of the URL, including sub-domains.", "e.g. login.github.com",
       "e.g. www.example.com", true, false, 3, 100},
      {"url_domain", "URL domain", "Registered domain of the URL, without sub-domains.", "e.g. github.com, stripe.com",
       "e.g. google.com", true, false, 3, 100},
      {"url_scheme", "Scheme", "Protocol of the URL.", "e.g. https, ftp", "e.g. http", false, false, 2, 20},
      {"url_tld", "URL TLD", "Top-level domain of the URL, without the dot.", "e.g. com, org, fr", "e.g. ru, cn", false,
       false, 2, 10},
      {"url_port", "Port", "Port number in the URL.", "e.g. 443, 8080", "e.g. 80", false, true, 1, 5},
      {"username", "Username / email", "The leaked login: an email address or a username.", "e.g. admin, john@company.com",
       "e.g. test, sample, noreply", true, false, 3, 100},
      {"password", "Password", "The leaked password.", "e.g. password123", "e.g. changeme, temp", true, false, 3, 100},
      {"username_hash", "Username hash (SHA-1 prefix)", "First hex characters of the SHA-1 of the username.",
       "e.g. 5baa6 (SHA-1 prefix)", nullptr, false, false, 4, 40},
      {"password_hash", "Password hash (SHA-1 prefix)", "First hex characters of the SHA-1 of the password.",
       "e.g. 5baa6 (SHA-1 prefix)", nullptr, false, false, 4, 40},
      {"email_host", "Email host", "Part after @, including sub-domains.", "e.g. gmail.com, outlook.com",
       "e.g. tempmail.com", true, false, 3, 100},
      {"email_domain", "Email domain", "Registered domain of the email address.", "e.g. company.com", "e.g. gmail.com",
       true, false, 3, 100},
      {"email_tld", "Email TLD", "Top-level domain of the email address, without the dot.", "e.g. com, org",
       "e.g. ru, cn", false, false, 2, 10},
  };
  static_assert(sizeof(infos) / sizeof(infos[0]) == static_cast<size_t>(AdvField::Count));
  return infos[static_cast<int>(f)];
}

int AdvQuery::valueCount() const {
  int n = 0;
  for (const auto& f : fields) n += static_cast<int>(f.include.size() + f.exclude.size());
  return n;
}

std::string advValueError(AdvField f, const std::string& value) {
  const AdvFieldInfo& info = advFieldInfo(f);
  if (static_cast<int>(value.size()) < info.minLen)
    return "At least " + std::to_string(info.minLen) + " characters.";
  if (static_cast<int>(value.size()) > info.maxLen) return "At most " + std::to_string(info.maxLen) + " characters.";
  if (info.numeric) {
    if (value.find_first_not_of("0123456789") != std::string::npos) return "Numbers only.";
    const long port = std::atol(value.c_str());
    if (port < 1 || port > 65535) return "Ports go from 1 to 65535.";
  }
  if ((f == AdvField::UsernameHash || f == AdvField::PasswordHash) &&
      value.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
    return "Hex characters only (0-9, a-f).";
  return {};
}

bool isIsoDate(const std::string& s) {
  if (s.size() != 10 || s[4] != '-' || s[7] != '-') return false;
  for (int i : {0, 1, 2, 3, 5, 6, 8, 9})
    if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
  const int m = std::atoi(s.substr(5, 2).c_str()), d = std::atoi(s.substr(8, 2).c_str());
  return m >= 1 && m <= 12 && d >= 1 && d <= 31;
}

const char* strengthLabel(std::optional<int> s) {
  if (!s) return "-";
  if (*s <= 2) return "Too weak";
  if (*s <= 4) return "Weak";
  if (*s <= 7) return "Medium";
  return "Strong";
}

LeakRow parseStealerRow(const json& j) {
  LeakRow r;
  r.id = idString(j);
  r.url = str(j, "url");
  r.username = str(j, "username");
  if (r.username.empty()) r.username = str(j, "username_masked");
  r.password = str(j, "password");
  if (auto s = optInt(j, "password_strength")) r.strength = static_cast<int>(*s);
  r.unlocked = boolean(j, "unlocked");
  r.addedAt = str(j, "added_at");
  r.info = categoryLabel(str(j, "category"));
  if (auto it = j.find("is_email"); it != j.end() && it->is_boolean()) r.isEmail = it->get<bool>();
  return r;
}

LeakPage parseLeakPage(const json& j) {
  LeakPage p;
  if (auto it = j.find("items"); it != j.end() && it->is_array()) {
    p.items.reserve(it->size());
    for (const auto& item : *it) p.items.push_back(parseStealerRow(item));
  }
  p.total = optInt(j, "total").value_or(static_cast<int64_t>(p.items.size()));
  p.totalUnlocked = optInt(j, "total_unlocked").value_or(0);
  p.page = static_cast<int>(optInt(j, "page").value_or(1));
  p.pageSize = static_cast<int>(optInt(j, "page_size").value_or(100));
  p.blacklisted = !str(j, "blacklisted_value").empty();
  return p;
}

DomainSummary parseDomainSummary(const json& j) {
  DomainSummary d;
  d.employees = optInt(j, "employees_compromised").value_or(0);
  d.customers = optInt(j, "customers_compromised").value_or(0);
  d.thirdParties = optInt(j, "third_parties_compromised").value_or(0);
  d.approximate = boolean(j, "counts_approximate");
  d.blacklisted = !str(j, "blacklisted_value").empty();
  return d;
}

Usage parseUsage(const json& j) {
  Usage u;
  if (auto it = j.find("requests_daily"); it != j.end() && it->is_object()) {
    u.used = optInt(*it, "used");
    u.limit = optInt(*it, "limit");
  }
  u.resetEpoch = optInt(j, "requests_daily_reset_epoch");
  return u;
}

Profile parseProfile(const json& j) {
  Profile p;
  p.firstName = str(j, "first_name");
  p.lastName = str(j, "last_name");
  p.email = str(j, "email");
  p.subscriptionActive = boolean(j, "subscription_active");
  auto sp = optInt(j, "subscription_points"), ep = optInt(j, "extra_points");
  if (sp || ep) p.points = sp.value_or(0) + ep.value_or(0);
  auto sg = optInt(j, "subscription_gb"), eg = optInt(j, "extra_gb");
  if (sg || eg) p.quotaBytes = sg.value_or(0) + eg.value_or(0);
  if (auto plan = j.find("plan"); plan != j.end() && plan->is_object()) {
    p.planName = str(*plan, "name");
    if (p.planName.empty()) p.planName = str(*plan, "plan_name");
    p.bypassPoints = boolean(*plan, "bypass_points");
  }
  return p;
}

LiveStats parseLiveStats(const json& j) {
  LiveStats s;
  if (!j.contains("raw_lines_total")) return s;  // {"message": "... not available"}
  s.rawLinesTotal = optInt(j, "raw_lines_total").value_or(0);
  s.rawLines7d = optInt(j, "raw_lines_7d").value_or(0);
  s.leaksTotal = optInt(j, "leaks_total").value_or(0);
  s.leaks7d = optInt(j, "leaks_7d").value_or(0);
  s.darkWebPosts = optInt(j, "dark_web_posts_total").value_or(0);
  if (auto it = j.find("credentials_per_second_24h"); it != j.end() && it->is_number())
    s.linesPerSecond24h = it->get<double>();
  s.sourcesActive = optInt(j, "sources_active").value_or(0);
  if (auto it = j.find("sparkline_24h"); it != j.end() && it->is_array())
    for (const auto& v : *it) s.sparkline24h.push_back(v.is_number() ? v.get<int64_t>() : 0);
  s.available = true;
  return s;
}

namespace {
DatasetStats parseDataset(const json& j, const char* key) {
  DatasetStats d;
  auto it = j.find(key);
  if (it == j.end() || !it->is_object()) return d;
  const json& o = *it;
  d.total = optInt(o, "total").value_or(0);
  d.today = optInt(o, "today").value_or(0);
  d.thisWeek = optInt(o, "this_week").value_or(0);
  d.thisMonth = optInt(o, "this_month").value_or(0);
  if (auto w = o.find("per_week"); w != o.end() && w->is_array()) {
    for (const auto& pt : *w) {
      if (!pt.is_object()) continue;
      d.weeks.push_back(str(pt, "week"));
      d.perWeek.push_back(optInt(pt, "count").value_or(0));
    }
  }
  d.present = true;
  return d;
}
}  // namespace

IndexStats parseIndexStats(const json& j) {
  IndexStats s;
  if (!j.contains("leaks") && !j.contains("raw_lines")) return s;  // {"message": "Stats not available"}
  s.leaks = parseDataset(j, "leaks");
  s.rawLines = parseDataset(j, "raw_lines");
  s.combolists = parseDataset(j, "combolists");
  if (auto it = j.find("dark_web_posts"); it != j.end() && it->is_object())
    s.darkWebPosts = optInt(*it, "total").value_or(0);
  s.available = true;
  return s;
}

DarkWebPage parseDarkWebPage(const json& j) {
  DarkWebPage p;
  if (auto it = j.find("items"); it != j.end() && it->is_array()) {
    for (const auto& x : *it) {
      DarkWebPost d;
      d.id = idString(x);
      d.title = str(x, "title");
      d.content = str(x, "content");
      d.source = str(x, "source_name");
      if (d.source.empty()) d.source = str(x, "source");
      d.author = str(x, "author");
      d.target = str(x, "target");
      d.sourceRef = str(x, "source_ref");
      d.publishedAt = str(x, "published_at");
      d.ingestedAt = str(x, "ingested_at");
      d.truncated = boolean(x, "truncated");
      d.censored = boolean(x, "censored");
      p.items.push_back(std::move(d));
    }
  }
  p.total = optInt(j, "total").value_or(static_cast<int64_t>(p.items.size()));
  p.page = static_cast<int>(optInt(j, "page").value_or(1));
  p.pageSize = static_cast<int>(optInt(j, "page_size").value_or(25));
  return p;
}

std::string formatThousands(int64_t v) {
  std::string digits = std::to_string(v < 0 ? -v : v);
  std::string out;
  for (size_t i = 0; i < digits.size(); ++i) {
    if (i && (digits.size() - i) % 3 == 0) out.push_back(',');
    out.push_back(digits[i]);
  }
  return v < 0 ? "-" + out : out;
}

std::string formatCompact(double v) {
  const char* suffix = "";
  if (v >= 1e12) { v /= 1e12; suffix = "T"; }
  else if (v >= 1e9) { v /= 1e9; suffix = "B"; }
  else if (v >= 1e6) { v /= 1e6; suffix = "M"; }
  else if (v >= 1e3) { v /= 1e3; suffix = "K"; }
  char buf[32];
  if (*suffix && v < 10.0) snprintf(buf, sizeof buf, "%.1f%s", v, suffix);
  else snprintf(buf, sizeof buf, "%.0f%s", v, suffix);
  return buf;
}

std::string formatBytes(int64_t bytes) {
  const char* units[] = {"B", "KB", "MB", "GB", "TB"};
  double v = static_cast<double>(bytes);
  int u = 0;
  while (v >= 1024.0 && u < 4) { v /= 1024.0; ++u; }
  char buf[32];
  snprintf(buf, sizeof buf, u == 0 ? "%.0f %s" : "%.1f %s", v, units[u]);
  return buf;
}

json buildAdvancedBody(const AdvQuery& q) {
  static const char* matchNames[] = {"contains", "starts_with", "ends_with"};
  json body = json::object();
  auto put = [&body](const std::string& key, const AdvFieldInfo& info, const std::vector<std::string>& values) {
    json arr = json::array();
    for (const auto& raw : values) {
      const std::string v = trim(raw);
      if (v.empty()) continue;
      if (info.numeric) arr.push_back(std::atoi(v.c_str()));
      else arr.push_back(v);
    }
    if (!arr.empty()) body[key] = std::move(arr);
    return body.contains(key);
  };
  for (int i = 0; i < static_cast<int>(AdvField::Count); ++i) {
    const AdvFieldInfo& info = advFieldInfo(static_cast<AdvField>(i));
    const AdvFilter& f = q.fields[i];
    const std::string key = info.key;
    if (put(key, info, f.include) && info.matchType && f.includeMatch != MatchType::Contains)
      body[key + "_match_type"] = matchNames[static_cast<int>(f.includeMatch)];
    if (info.excludeHint && put(key + "_not", info, f.exclude) && info.matchType && f.excludeMatch != MatchType::Contains)
      body[key + "_not_match_type"] = matchNames[static_cast<int>(f.excludeMatch)];
  }
  if (q.idKind == IdKind::Email) body["is_email"] = true;
  if (q.idKind == IdKind::Username) body["is_email"] = false;
  static const char* strengths[] = {nullptr, "too_weak", "weak", "medium", "strong"};
  if (q.strength >= 1 && q.strength <= 4) body["password_strength"] = strengths[q.strength];
  if (isIsoDate(trim(q.addedFrom))) body["added_from"] = trim(q.addedFrom) + "T00:00:00Z";
  if (isIsoDate(trim(q.addedTo))) body["added_to"] = trim(q.addedTo) + "T23:59:59Z";
  if (q.forceAnd) body["force_and"] = true;
  return body;
}

std::string leakKey(const LeakRow& r) {
  if (!r.id.empty()) return "id:" + r.id;
  // Fallback when IDs are hidden by the plan: content-based identity.
  return "c:" + r.url + "|" + r.username + "|" + r.addedAt + "|" + r.info;
}

}  // namespace su
