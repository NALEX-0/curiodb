#include <gtest/gtest.h>

#include "curiodb/catalog/catalog.hpp"
#include "curiodb/storage/in_memory_storage.hpp"
#include "curiodb/types/data_type.hpp"

namespace curiodb::storage {
namespace {

TEST(InMemoryStorageTest, KeepsTablesByDatabaseAndName) {
  InMemoryStorage storage;
  const catalog::TableSchema schema{
      .name = "Employees",
      .columns = {{.name = "id",
                   .type = {.kind = DataTypeKind::Integer}}},
  };

  storage.create_table("Company", schema);

  EXPECT_NE(storage.find_table("company", "employees"), nullptr);
  EXPECT_EQ(storage.find_table("other", "employees"), nullptr);
}

}  // namespace
}  // namespace curiodb::storage

