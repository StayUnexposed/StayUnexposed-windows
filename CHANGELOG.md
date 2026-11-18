# Changelog

All notable changes to Stay Unexposed are documented in this file.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses
[Semantic Versioning](https://semver.org/): **major** for breaking changes, **minor** for new features,
**patch** for fixes only. The current version is in the `VERSION` file.

---

## [1.4.1] - 2026-10-28

### Monitoring
- **Fixed:** the daily check could run twice when the PC woke from sleep within a minute of the
  scheduled time. A run is now recorded before it starts, so a second trigger is ignored.
- **Fixed:** removing a value while it was being checked could leave a stale "Checking…" label on the
  watchlist until the app was restarted.

### Results table
- **Fixed:** very long URLs overlapped the *Username* column on narrow windows. They are now shortened
  with an ellipsis, and the full URL is shown on hover.

### System tray
- **Fixed:** the tray icon disappeared after Windows Explorer restarted. The app now listens for
  `TaskbarCreated` and adds the icon back.

---

## [1.4.0] - 2026-10-14

### Dark web search
- **Added:** a new **Dark web** tab next to *Email / Domain*. Search posts from forums and messaging
  channels by keyword, company name or domain.
- **Added:** each post shows its title, source, author, target and publication date, with a preview
  of the content.
- **Added:** results are paged 25 posts at a time.

### Export
- **Added:** a progress notification while an export is running, showing rows written out of the
  expected total.
- **Added:** when the export finishes, the notification links to the CSV file in the Downloads folder.
- **Changed:** exports are capped at 10,000 rows to keep files manageable in spreadsheet tools.

---

## [1.3.2] - 2026-09-30

### Advanced search
- **Fixed:** pasting a comma- or newline-separated list into a filter field now splits it into one
  chip per value instead of a single long value.
- **Fixed:** removing the last chip from a field no longer clears the match type that was chosen for it.

### Results table
- **Fixed:** sorting by *Strength* now orders passwords from weakest to strongest.

---

## [1.3.1] - 2026-09-19

### Dashboard
- **Changed:** the index growth card now loads in the background, so the search page opens
  immediately on start-up.

### Monitoring
- **Fixed:** a leak already present in the baseline could raise an alert again when the API returned
  the login with different letter case. Logins are now compared case-insensitively.

---

## [1.3.0] - 2026-09-05

### Advanced search
- **Added:** an **Advanced search** option on the *Email / Domain* tab for building precise queries.
- **Added:** include and exclude filters on 13 fields:
  - **URL:** full URL, host, registered domain, scheme, TLD and port
  - **Login:** username / email, and a SHA-1 prefix of the username
  - **Password:** password, and a SHA-1 prefix of the password
  - **Email:** host, registered domain and TLD
- **Added:** each filter can match values that *contain*, *start with* or *end with* the text.
- **Added:** a built-in examples panel, e.g. *Admin panels of one company*, *Company accounts used on
  GitHub* and *Leave out test accounts*.
- **Added:** once a search runs, the form collapses to a one-line summary of the active filters.

### Domain search
- **Added:** a summary of affected accounts for a domain, split into **employees**, **third parties**
  and **customers**.
- **Added:** filter the leak list by category, or view all categories together.

---

## [1.2.0] - 2026-08-21

### Search
- **Added:** filter email / username results to *email only*, *username only* or *both*.
- **Added:** a text filter to narrow the current results without running a new search.

### Password unlocking
- **Added:** passwords can be revealed directly in the results table using the eye icon.
- **Added:** up to the first 30 rows can be unlocked; rows are processed one at a time with a short
  pause between them, so the plan's rate limits are respected.

### Account
- **Added:** your plan, remaining points and usage are shown in the header.

---

## [1.1.0] - 2026-08-07

### Dashboard
- **Added:** an **Index growth** card on the search page with three panels:
  - weekly growth over the last 12 months for stealer logs and raw lines, as a running total
  - a live indexed-lines counter with leaks, lines this week and lines per minute
  - raw lines added per hour over the last 24 hours
- **Added:** the card can be hidden and remembers that choice.

### Search
- **Changed:** email, username and domain searches now share a single input. The app detects the
  type of value automatically, and the detection can be overridden from a drop-down.

### Export
- **Added:** export any search result to CSV in the Downloads folder, with a unique file name per
  search (e.g. `leaks_acme-corp.com.csv`).
- **Added:** fields are escaped according to RFC 4180, and values that a spreadsheet would treat as a
  formula are neutralised to prevent formula injection.

### Startup and tray
- **Added:** a **Start with Windows** option (per user, no administrator rights required).
- **Added:** a **Keep running in the tray when closed** option, so daily checks continue in the
  background.
- **Added:** only one copy of the app runs at a time; starting it again brings the existing window
  to the front.

---

## [1.0.0] - 2026-07-24

First public release.

### Search
- **Added:** search leaked stealer-log credentials by email address or username.
- **Added:** results show the URL, login, password status, date added and password strength, paged
  100 rows at a time.
- **Added:** protected values are reported clearly instead of returning an empty result.

### Monitoring
- **Added:** a **Monitoring** page to save the emails and usernames you want to watch.
- **Added:** adding a value records its current leaks as a baseline, so you are not alerted about
  leaks you already know.
- **Added:** every value is re-checked once a day at a time you choose (default 08:00). If the PC
  was off at that time, the check runs at the next start-up.
- **Added:** a **Check now** action per value, and confirmation before a value is removed.
- **Added:** a Windows desktop notification for every leak that has not been seen before.

### Privacy
- **Added:** the watchlist, known leaks and alerts are stored locally on your PC and are never
  uploaded.
