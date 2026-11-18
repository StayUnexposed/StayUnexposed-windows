#include "api/MockData.h"

#include <algorithm>
#include <cstdio>

namespace su::mock {

LeakPage leakPage(int page, int pageSize) {
  struct Row {
    const char* url;
    const char* user;
    const char* pass;
    const char* date;
    const char* info;
    bool email;
  };
  static const Row rows[] = {
      {"acme-corp.com/login", "j.martin@acme-corp.com", "Acm3Corp!2023", "2026-06-07", "Employee", true},
      {"login.microsoftonline.com", "s.bernard@acme-corp.com", "Summer@2024", "2026-06-03", "Employee", true},
      {"github.com/login", "admin@acme-corp.com", "Adm1n$ecure9", "2026-05-21", "Third party", true},
      {"www.linkedin.com/login", "p.durand@acme-corp.com", "Qwerty!789xZ", "2025-08-15", "Employee", true},
      {"dropbox.com/login", "c.leroy@acme-corp.com", "Dr0pB0x#55", "2026-02-10", "Third party", true},
      {"vpn.acme-corp.com", "jdurand", "V3rySecret!", "2024-12-10", "Employee", false},
      {"shop.acme-corp.com/account", "m.petit@gmail.com", "shopping12", "2024-10-02", "Customer", true},
  };
  LeakPage p;
  p.page = page;
  p.pageSize = pageSize;
  p.total = 147;
  p.totalUnlocked = 147;
  int n = (page - 1) * 7;
  for (const Row& r : rows) {
    LeakRow row;
    char id[32];
    std::snprintf(id, sizeof id, "s%d", n++);
    row.id = id;
    row.url = r.url;
    row.username = r.user;
    row.password = r.pass;
    row.unlocked = true;
    row.isEmail = r.email;
    row.addedAt = std::string(r.date) + "T00:00:00Z";
    row.info = r.info;
    row.strength = 6;
    p.items.push_back(row);
  }
  return p;
}

DomainSummary domainSummary() {
  DomainSummary d;
  d.employees = 89;
  d.thirdParties = 41;
  d.customers = 17;
  return d;
}

DarkWebPage darkWebPage(int page, int pageSize) {
  DarkWebPage dw;
  dw.page = page;
  dw.pageSize = pageSize;
  dw.total = 23;
  static const char* titles[] = {"Database dump: acme-corp.com (sample)", "Combolist 2026 fresh (sample)",
                                  "Access to corporate VPN (sample)"};
  static const char* sources[] = {"BreachForums", "Telegram", "XSS"};
  for (int i = 0; i < 3; ++i) {
    DarkWebPost post;
    char id[32];
    std::snprintf(id, sizeof id, "dw%d", (page - 1) * 3 + i);
    post.id = id;
    post.title = titles[i];
    post.content = "This is sample dark-web content No real data.";
    post.source = sources[i];
    post.author = "seller_sample";
    post.target = "acme-corp.com";
    post.publishedAt = "2026-06-0" + std::to_string(i + 1) + "T12:00:00Z";
    dw.items.push_back(post);
  }
  return dw;
}

Usage usage() {
  Usage u;
  u.used = 240;
  u.limit = 1000;
  return u;
}

Profile profile() {
  Profile p;
  p.firstName = "Sample";
  p.lastName = "User";
  p.email = "user@stayunexposed.com";
  p.planName = "Pro";
  p.points = 760;
  p.quotaBytes = 5LL * 1024 * 1024 * 1024;
  p.bypassPoints = true;  // every record readable without unlocking
  p.subscriptionActive = true;
  return p;
}

LiveStats liveStats() {
  LiveStats s;
  s.rawLinesTotal = 831632049166;
  s.leaksTotal = 196400;
  s.rawLines7d = 5800000000;
  s.linesPerSecond24h = 18677.47;
  s.sourcesActive = 3;
  for (int i = 0; i < 24; ++i)
    s.sparkline24h.push_back(5200000 + ((i * 7919) % 13) * 310000 + (i == 23 ? -3100000 : 0));
  s.available = true;
  return s;
}

IndexStats indexStats() {
  IndexStats ix;
  auto series = [](DatasetStats& d, double start, double end, int64_t today, int64_t week, int64_t month) {
    for (int i = 0; i < 52; ++i) {
      int dayOfYear = 321 + i * 7;
      int year = 2025 + dayOfYear / 365, doy = dayOfYear % 365;
      static const int mdays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
      int m = 0;
      while (doy >= mdays[m]) doy -= mdays[m++];
      char buf[16];
      std::snprintf(buf, sizeof buf, "%04d-%02d-%02d", year, m + 1, doy + 1);
      d.weeks.push_back(buf);
      double t = i / 51.0;
      d.perWeek.push_back(static_cast<int64_t>(start + (end - start) * (t * t * 0.6 + t * 0.4)));
    }
    std::sort(d.perWeek.begin(), d.perWeek.end());
    d.total = d.perWeek.back();
    d.today = today;
    d.thisWeek = week;
    d.thisMonth = month;
    d.present = true;
  };
  series(ix.leaks, 2.54e9, 4.63e9, 592881, 15292650, 115388673);
  series(ix.rawLines, 1.2e10, 8.0e11, 162742886, 5243268891, 70510127713);
  series(ix.combolists, 0, 6.37e9, 0, 1604194, 34313392);
  ix.available = true;
  return ix;
}

}  // namespace su::mock
