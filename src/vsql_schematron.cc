/* Copyright (c) 2026 VillageSQL Contributors
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include <villagesql/vsql.h>
#include <villagesql/preview/sql_query.h>
#include <villagesql/preview/thread_worker.h>
#include <string>
#include <unordered_set>
#include <shared_mutex>
#include <mutex>
#include <cstring>

using namespace vsql;

static vsql::preview_sql_query::SqlQueryCapability g_sql_query_cap;

namespace {

std::shared_mutex g_schema_cache_mutex;
std::unordered_set<std::string> g_schema_cache;

void updateSchemaCache(struct vef_thread_handle_t *handle) {
    auto session = g_sql_query_cap.open(handle);
    if (!session) {
        return;
    }

    std::string sql = "SELECT SCHEMA_NAME FROM INFORMATION_SCHEMA.SCHEMATA";

    auto result = session.sql(sql).execute();
    if (!result || result.has_error()) {
        return;
    }

    std::unordered_set<std::string> temp_cache;
    while (result.next()) {
        std::string_view db_name_sv = result.column_str(0);
        if (db_name_sv.empty()) {
            continue;
        }
        temp_cache.insert(std::string(db_name_sv));
    }

    std::unique_lock<std::shared_mutex> lock(g_schema_cache_mutex);
    g_schema_cache = std::move(temp_cache);
}

vef_next_wakeup_t schema_cache_worker(vef_wakeup_reason_t reason,
                                      struct vef_thread_handle_t *handle,
                                      void *) {
    if (reason == VEF_WAKEUP_ENABLE) {
        return {1, -1};
    }
    if (reason == VEF_WAKEUP_DISABLE) {
        return {};
    }
    if (reason == VEF_WAKEUP_PERIODIC) {
        updateSchemaCache(handle);
        return {5000, -1};
    }
    return {};
}

} // namespace

static vsql::preview_thread_worker::ThreadWorkerCapability<&schema_cache_worker>
    g_thread_worker_cap{"schema_cache"};

void hello_world_impl(StringResult out) {
  const char* hello = "Hello, World!";
  auto buf = out.buffer();
  memcpy(buf.data(), hello, strlen(hello));
  out.set_length(strlen(hello));
}

void vsql_schema_cache_ready_impl(StringArg db_name, IntResult out) {
    if (db_name.is_null()) {
        out.set(0);
        return;
    }
    std::shared_lock<std::shared_mutex> lock(g_schema_cache_mutex);
    std::string db_str(db_name.value());
    out.set(g_schema_cache.find(db_str) != g_schema_cache.end() ? 1 : 0);
}

VEF_GENERATE_ENTRY_POINTS(
  make_extension()
    .with(g_sql_query_cap)
    .with(g_thread_worker_cap)
    .func(make_func<&hello_world_impl>("hello_world")
      .returns(STRING)
      .no_params()
      .buffer_size(14)
      .build())
    .func(make_func<&vsql_schema_cache_ready_impl>("vsql_schema_cache_ready")
      .returns(INT)
      .param(STRING)
      .build())
)
