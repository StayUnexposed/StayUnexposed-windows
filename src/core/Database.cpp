#include "core/Database.h"

#include <sqlite3.h>

namespace su {

namespace {

const char* kSchema = R"SQL(
PRAGMA journal_mode=WAL;
PRAGMA foreign_keys=ON;
CREATE TABLE IF NOT EXISTS settings(
  key   TEXT PRIMARY KEY,
  value TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS watch_item(
  id            INTEGER PRIMARY KEY,
  value         TEXT NOT NULL,
  kind          INTEGER NOT NULL,
  created_at    INTEGER NOT NULL,
  last_check_at INTEGER NOT NULL DEFAULT 0,
  stealer_total INTEGER NOT NULL DEFAULT -1,
  combo_total   INTEGER NOT NULL DEFAULT -1,
  last_status   TEXT NOT NULL DEFAULT '',
  baseline_done INTEGER NOT NULL DEFAULT 0,
  UNIQUE(value COLLATE NOCASE, kind));
CREATE TABLE IF NOT EXISTS alert(
  id         INTEGER PRIMARY KEY,
  watch_id   INTEGER NOT NULL REFERENCES watch_item(id) ON DELETE CASCADE,
  created_at INTEGER NOT NULL,
  new_count  INTEGER NOT NULL,
  sources    TEXT NOT NULL DEFAULT '',
  read       INTEGER NOT NULL DEFAULT 0);
CREATE TABLE IF NOT EXISTS seen_leak(
  watch_id      INTEGER NOT NULL REFERENCES watch_item(id) ON DELETE CASCADE,
  source        INTEGER NOT NULL,
  leak_key      TEXT NOT NULL,
  url           TEXT NOT NULL DEFAULT '',
  username      TEXT NOT NULL DEFAULT '',
  strength      INTEGER,
  added_at      TEXT NOT NULL DEFAULT '',
  info          TEXT NOT NULL DEFAULT '',
  first_seen_at INTEGER NOT NULL,
  alert_id      INTEGER NOT NULL DEFAULT 0,
  PRIMARY KEY(watch_id, source, leak_key));
CREATE INDEX IF NOT EXISTS seen_leak_alert ON seen_leak(alert_id);
)SQL";

// RAII prepared statement.
class Stmt {
 public:
  Stmt(sqlite3* db, const char* sql) { sqlite3_prepare_v2(db, sql, -1, &s_, nullptr); }
  ~Stmt() { sqlite3_finalize(s_); }
  Stmt(const Stmt&) = delete;
  Stmt& operator=(const Stmt&) = delete;

  explicit operator bool() const { return s_ != nullptr; }
  Stmt& bind(int i, int64_t v) { sqlite3_bind_int64(s_, i, v); return *this; }
  Stmt& bind(int i, const std::string& v) { sqlite3_bind_text(s_, i, v.c_str(), static_cast<int>(v.size()), SQLITE_TRANSIENT); return *this; }
  Stmt& bindNull(int i) { sqlite3_bind_null(s_, i); return *this; }
  bool step() { return s_ && sqlite3_step(s_) == SQLITE_ROW; }
  bool run() { return s_ && sqlite3_step(s_) == SQLITE_DONE; }
  void reset() { sqlite3_reset(s_); sqlite3_clear_bindings(s_); }

  int64_t i64(int c) const { return sqlite3_column_int64(s_, c); }
  bool isNull(int c) const { return sqlite3_column_type(s_, c) == SQLITE_NULL; }
  std::string text(int c) const {
    auto p = reinterpret_cast<const char*>(sqlite3_column_text(s_, c));
    return p ? std::string(p, static_cast<size_t>(sqlite3_column_bytes(s_, c))) : std::string();
  }

 private:
  sqlite3_stmt* s_ = nullptr;
};

LeakRow rowFromSeen(const Stmt& s, int base) {
  LeakRow r;
  r.url = s.text(base + 0);
  r.username = s.text(base + 1);
  if (!s.isNull(base + 2)) r.strength = static_cast<int>(s.i64(base + 2));
  r.addedAt = s.text(base + 3);
  r.info = s.text(base + 4);
  return r;
}

}  // namespace

const char* watchKindLabel(WatchKind k) {
  switch (k) {
    case WatchKind::Email: return "Email";
    case WatchKind::Username: return "Username";
    case WatchKind::Domain: return "Domain";
  }
  return "?";
}

const char* leakSourceLabel(LeakSource s) {
  switch (s) {
    case LeakSource::Stealer: return "Stealer logs";
    case LeakSource::Combolist: return "Combolists";
    case LeakSource::Domain: return "Domain leaks";
  }
  return "?";
}

Database::~Database() {
  if (db_) sqlite3_close(db_);
}

bool Database::exec(const char* sql) {
  char* err = nullptr;
  if (sqlite3_exec(db_, sql, nullptr, nullptr, &err) != SQLITE_OK) {
    lastError_ = err ? err : "sqlite error";
    sqlite3_free(err);
    return false;
  }
  return true;
}

bool Database::open(const std::filesystem::path& file) {
  std::lock_guard lock(mutex_);
  const std::u8string u8 = file.u8string();
  std::string path(u8.begin(), u8.end());
  if (path.empty()) path = ":memory:";
  if (sqlite3_open_v2(path.c_str(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                      nullptr) != SQLITE_OK) {
    lastError_ = db_ ? sqlite3_errmsg(db_) : "cannot open database";
    return false;
  }
  sqlite3_busy_timeout(db_, 5000);
  return exec(kSchema);
}

std::string Database::getSetting(const std::string& key, const std::string& fallback) {
  std::lock_guard lock(mutex_);
  Stmt s(db_, "SELECT value FROM settings WHERE key=?");
  s.bind(1, key);
  return s.step() ? s.text(0) : fallback;
}

void Database::setSetting(const std::string& key, const std::string& value) {
  std::lock_guard lock(mutex_);
  Stmt s(db_, "INSERT INTO settings(key,value) VALUES(?,?) ON CONFLICT(key) DO UPDATE SET value=excluded.value");
  s.bind(1, key).bind(2, value).run();
}

int64_t Database::addWatch(const std::string& value, WatchKind kind) {
  std::lock_guard lock(mutex_);
  Stmt s(db_, "INSERT OR IGNORE INTO watch_item(value,kind,created_at) VALUES(?,?,strftime('%s','now'))");
  s.bind(1, value).bind(2, static_cast<int64_t>(kind));
  if (!s.run() || sqlite3_changes(db_) == 0) return 0;
  return sqlite3_last_insert_rowid(db_);
}

void Database::removeWatch(int64_t id) {
  std::lock_guard lock(mutex_);
  Stmt s(db_, "DELETE FROM watch_item WHERE id=?");
  s.bind(1, id).run();
}

static const char* kWatchSelect =
    "SELECT w.id,w.value,w.kind,w.created_at,w.last_check_at,w.stealer_total,w.combo_total,w.last_status,"
    "w.baseline_done,"
    "(SELECT COALESCE(SUM(a.new_count),0) FROM alert a WHERE a.watch_id=w.id AND a.read=0) "
    "FROM watch_item w ";

static WatchItem watchFromRow(const Stmt& s) {
  WatchItem w;
  w.id = s.i64(0);
  w.value = s.text(1);
  w.kind = static_cast<WatchKind>(s.i64(2));
  w.createdAt = s.i64(3);
  w.lastCheckAt = s.i64(4);
  w.stealerTotal = s.i64(5);
  w.comboTotal = s.i64(6);
  w.lastStatus = s.text(7);
  w.baselineDone = s.i64(8) != 0;
  w.unreadNew = s.i64(9);
  return w;
}

std::vector<WatchItem> Database::listWatches() {
  std::lock_guard lock(mutex_);
  std::vector<WatchItem> out;
  Stmt s(db_, (std::string(kWatchSelect) + "ORDER BY w.value COLLATE NOCASE").c_str());
  while (s.step()) out.push_back(watchFromRow(s));
  return out;
}

bool Database::getWatch(int64_t id, WatchItem& out) {
  std::lock_guard lock(mutex_);
  Stmt s(db_, (std::string(kWatchSelect) + "WHERE w.id=?").c_str());
  s.bind(1, id);
  if (!s.step()) return false;
  out = watchFromRow(s);
  return true;
}

void Database::updateWatchResult(int64_t id, int64_t checkedAt, int64_t stealerTotal, int64_t comboTotal,
                                 const std::string& status, bool baselineDone) {
  std::lock_guard lock(mutex_);
  Stmt s(db_,
         "UPDATE watch_item SET last_check_at=?,stealer_total=?,combo_total=?,last_status=?,baseline_done=? "
         "WHERE id=?");
  s.bind(1, checkedAt).bind(2, stealerTotal).bind(3, comboTotal).bind(4, status).bind(5, baselineDone ? 1 : 0)
      .bind(6, id)
      .run();
}

std::unordered_set<std::string> Database::seenKeys(int64_t watchId, LeakSource source) {
  std::lock_guard lock(mutex_);
  std::unordered_set<std::string> keys;
  Stmt s(db_, "SELECT leak_key FROM seen_leak WHERE watch_id=? AND source=?");
  s.bind(1, watchId).bind(2, static_cast<int64_t>(source));
  while (s.step()) keys.insert(s.text(0));
  return keys;
}

void Database::insertSeen(int64_t watchId, LeakSource source, const std::vector<LeakRow>& rows, int64_t now,
                          int64_t alertId) {
  std::lock_guard lock(mutex_);
  exec("BEGIN");
  Stmt s(db_,
         "INSERT OR IGNORE INTO seen_leak(watch_id,source,leak_key,url,username,strength,added_at,info,"
         "first_seen_at,alert_id) VALUES(?,?,?,?,?,?,?,?,?,?)");
  for (const auto& r : rows) {
    s.bind(1, watchId).bind(2, static_cast<int64_t>(source)).bind(3, leakKey(r)).bind(4, r.url).bind(5, r.username);
    if (r.strength) s.bind(6, static_cast<int64_t>(*r.strength)); else s.bindNull(6);
    s.bind(7, r.addedAt).bind(8, r.info).bind(9, now).bind(10, alertId);
    s.run();
    s.reset();
  }
  exec("COMMIT");
}

static const char* kSeenColumns = "url,username,strength,added_at,info,source ";

static std::vector<LeakRow> readSeen(Stmt& s) {
  std::vector<LeakRow> out;
  while (s.step()) {
    LeakRow r = rowFromSeen(s, 0);
    const auto source = static_cast<LeakSource>(s.i64(5));
    const char* src = source == LeakSource::Combolist ? "Combolist"
                      : source == LeakSource::Domain  ? "Domain"
                                                      : "Stealer log";
    r.info = r.info.empty() ? src : std::string(src) + " - " + r.info;
    out.push_back(std::move(r));
  }
  return out;
}

std::vector<LeakRow> Database::watchLeaks(int64_t watchId) {
  std::lock_guard lock(mutex_);
  Stmt s(db_, (std::string("SELECT ") + kSeenColumns +
               "FROM seen_leak WHERE watch_id=? ORDER BY first_seen_at DESC, added_at DESC").c_str());
  s.bind(1, watchId);
  return readSeen(s);
}

int64_t Database::createAlert(int64_t watchId, int64_t now, int64_t newCount, const std::string& sources) {
  std::lock_guard lock(mutex_);
  Stmt s(db_, "INSERT INTO alert(watch_id,created_at,new_count,sources) VALUES(?,?,?,?)");
  s.bind(1, watchId).bind(2, now).bind(3, newCount).bind(4, sources).run();
  return sqlite3_last_insert_rowid(db_);
}

std::vector<AlertItem> Database::listAlerts(int limit) {
  std::lock_guard lock(mutex_);
  std::vector<AlertItem> out;
  Stmt s(db_,
         "SELECT a.id,a.watch_id,w.value,a.created_at,a.new_count,a.sources,a.read FROM alert a "
         "JOIN watch_item w ON w.id=a.watch_id ORDER BY a.created_at DESC, a.id DESC LIMIT ?");
  s.bind(1, static_cast<int64_t>(limit));
  while (s.step()) {
    AlertItem a;
    a.id = s.i64(0);
    a.watchId = s.i64(1);
    a.watchValue = s.text(2);
    a.createdAt = s.i64(3);
    a.newCount = s.i64(4);
    a.sources = s.text(5);
    a.read = s.i64(6) != 0;
    out.push_back(std::move(a));
  }
  return out;
}

std::vector<LeakRow> Database::alertLeaks(int64_t alertId) {
  std::lock_guard lock(mutex_);
  Stmt s(db_, (std::string("SELECT ") + kSeenColumns + "FROM seen_leak WHERE alert_id=? ORDER BY added_at DESC")
                  .c_str());
  s.bind(1, alertId);
  return readSeen(s);
}

void Database::markAlertRead(int64_t alertId) {
  std::lock_guard lock(mutex_);
  Stmt s(db_, "UPDATE alert SET read=1 WHERE id=?");
  s.bind(1, alertId).run();
}

void Database::markAllAlertsRead() {
  std::lock_guard lock(mutex_);
  exec("UPDATE alert SET read=1 WHERE read=0");
}

int64_t Database::unreadAlertCount() {
  std::lock_guard lock(mutex_);
  Stmt s(db_, "SELECT COUNT(*) FROM alert WHERE read=0");
  return s.step() ? s.i64(0) : 0;
}

}  // namespace su
