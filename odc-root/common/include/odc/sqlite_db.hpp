#pragma once

#include <sqlite3.h>

#include <memory>
#include <mutex>
#include <string>

namespace odc {

class SqliteDb {
public:
    SqliteDb() = default;
    ~SqliteDb();

    SqliteDb(const SqliteDb&) = delete;
    SqliteDb& operator=(const SqliteDb&) = delete;
    SqliteDb(SqliteDb&&) = delete;
    SqliteDb& operator=(SqliteDb&&) = delete;

    bool open(const std::string& path);
    void close();
    bool is_open() const;

    bool execute(const std::string& sql);

    class Statement {
    public:
        Statement(sqlite3* db, const std::string& sql);
        ~Statement();
        Statement(const Statement&) = delete;
        Statement& operator=(const Statement&) = delete;
        Statement(Statement&& other) noexcept;
        Statement& operator=(Statement&& other) noexcept;

        bool bind(int index, int value);
        bool bind(int index, int64_t value);
        bool bind(int index, double value);
        bool bind(int index, const std::string& value);
        bool bind(int index, std::nullptr_t);
        bool execute();

        bool step();
        int column_int(int index);
        int64_t column_int64(int index);
        double column_double(int index);
        std::string column_text(int index);
        bool column_is_null(int index);
        void reset();
        void clear_bindings();

        bool valid() const { return stmt_ != nullptr; }

    private:
        sqlite3_stmt* stmt_{nullptr};
    };

    Statement prepare(const std::string& sql);

    bool begin_transaction();
    bool commit();
    bool rollback();

    int schema_version();
    bool ensure_schema(int target_version, const std::string& migration_sql);

    int64_t last_insert_rowid();

private:
    sqlite3* db_{nullptr};
    std::recursive_mutex mutex_;
};

} // namespace odc
