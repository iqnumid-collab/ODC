#include "odc/sqlite_db.hpp"

#include <stdexcept>

namespace odc {

SqliteDb::~SqliteDb() {
    close();
}

bool SqliteDb::open(const std::string& path) {
    std::lock_guard lock(mutex_);
    if (db_) return true;

    int rc = sqlite3_open_v2(path.c_str(), &db_,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
        nullptr);
    if (rc != SQLITE_OK) {
        if (db_) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
        return false;
    }

    execute("PRAGMA journal_mode=WAL");
    execute("PRAGMA foreign_keys=ON");
    execute("PRAGMA busy_timeout=5000");

    return true;
}

void SqliteDb::close() {
    std::lock_guard lock(mutex_);
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool SqliteDb::is_open() const {
    return db_ != nullptr;
}

bool SqliteDb::execute(const std::string& sql) {
    std::lock_guard lock(mutex_);
    if (!db_) return false;

    char* err_msg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        if (err_msg) {
            sqlite3_free(err_msg);
        }
        return false;
    }
    return true;
}

SqliteDb::Statement SqliteDb::prepare(const std::string& sql) {
    std::lock_guard lock(mutex_);
    return Statement(db_, sql);
}

bool SqliteDb::begin_transaction() {
    return execute("BEGIN TRANSACTION");
}

bool SqliteDb::commit() {
    return execute("COMMIT");
}

bool SqliteDb::rollback() {
    return execute("ROLLBACK");
}

int SqliteDb::schema_version() {
    execute("CREATE TABLE IF NOT EXISTS schema_migrations (version INTEGER PRIMARY KEY)");
    auto stmt = prepare("SELECT MAX(version) FROM schema_migrations");
    if (stmt.valid() && stmt.step()) {
        return stmt.column_int(0);
    }
    return 0;
}

bool SqliteDb::ensure_schema(int target_version, const std::string& migration_sql) {
    int current = schema_version();
    if (current >= target_version) return true;

    if (!begin_transaction()) return false;

    if (!execute(migration_sql)) {
        rollback();
        return false;
    }

    auto stmt = prepare("INSERT INTO schema_migrations (version) VALUES (?)");
    for (int v = current + 1; v <= target_version; ++v) {
        stmt.reset();
        stmt.clear_bindings();
        stmt.bind(1, v);
        if (!stmt.execute()) {
            rollback();
            return false;
        }
    }

    return commit();
}

int64_t SqliteDb::last_insert_rowid() {
    std::lock_guard lock(mutex_);
    if (!db_) return 0;
    return sqlite3_last_insert_rowid(db_);
}

// ── Statement implementation ─────────────────────────────────────────────────

SqliteDb::Statement::Statement(sqlite3* db, const std::string& sql) {
    if (db) {
        sqlite3_prepare_v2(db, sql.c_str(), static_cast<int>(sql.size()),
                           &stmt_, nullptr);
    }
}

SqliteDb::Statement::~Statement() {
    if (stmt_) {
        sqlite3_finalize(stmt_);
    }
}

SqliteDb::Statement::Statement(Statement&& other) noexcept
    : stmt_(other.stmt_) {
    other.stmt_ = nullptr;
}

SqliteDb::Statement& SqliteDb::Statement::operator=(Statement&& other) noexcept {
    if (this != &other) {
        if (stmt_) sqlite3_finalize(stmt_);
        stmt_ = other.stmt_;
        other.stmt_ = nullptr;
    }
    return *this;
}

bool SqliteDb::Statement::bind(int index, int value) {
    if (!stmt_) return false;
    return sqlite3_bind_int(stmt_, index, value) == SQLITE_OK;
}

bool SqliteDb::Statement::bind(int index, int64_t value) {
    if (!stmt_) return false;
    return sqlite3_bind_int64(stmt_, index, value) == SQLITE_OK;
}

bool SqliteDb::Statement::bind(int index, double value) {
    if (!stmt_) return false;
    return sqlite3_bind_double(stmt_, index, value) == SQLITE_OK;
}

bool SqliteDb::Statement::bind(int index, const std::string& value) {
    if (!stmt_) return false;
    return sqlite3_bind_text(stmt_, index, value.c_str(),
                             static_cast<int>(value.size()),
                             SQLITE_TRANSIENT) == SQLITE_OK;
}

bool SqliteDb::Statement::bind(int index, std::nullptr_t) {
    if (!stmt_) return false;
    return sqlite3_bind_null(stmt_, index) == SQLITE_OK;
}

bool SqliteDb::Statement::execute() {
    if (!stmt_) return false;
    int rc = sqlite3_step(stmt_);
    return rc == SQLITE_DONE || rc == SQLITE_ROW;
}

bool SqliteDb::Statement::step() {
    if (!stmt_) return false;
    return sqlite3_step(stmt_) == SQLITE_ROW;
}

int SqliteDb::Statement::column_int(int index) {
    if (!stmt_) return 0;
    return sqlite3_column_int(stmt_, index);
}

int64_t SqliteDb::Statement::column_int64(int index) {
    if (!stmt_) return 0;
    return sqlite3_column_int64(stmt_, index);
}

double SqliteDb::Statement::column_double(int index) {
    if (!stmt_) return 0.0;
    return sqlite3_column_double(stmt_, index);
}

std::string SqliteDb::Statement::column_text(int index) {
    if (!stmt_) return {};
    auto* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt_, index));
    return text ? std::string(text) : std::string{};
}

bool SqliteDb::Statement::column_is_null(int index) {
    if (!stmt_) return true;
    return sqlite3_column_type(stmt_, index) == SQLITE_NULL;
}

void SqliteDb::Statement::reset() {
    if (stmt_) {
        sqlite3_reset(stmt_);
    }
}

void SqliteDb::Statement::clear_bindings() {
    if (stmt_) {
        sqlite3_clear_bindings(stmt_);
    }
}

} // namespace odc
