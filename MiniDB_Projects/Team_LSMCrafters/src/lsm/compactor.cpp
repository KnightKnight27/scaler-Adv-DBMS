#include "lsm/compactor.h"
#include <memory>
#include "lsm/merge_iterator.h"
#include "lsm/sstable_writer.h"

namespace minidb {

SSTableMeta Compactor::compact(const std::vector<SSTableReader*>& inputs,
                               const std::string& out_path) {
  std::vector<std::unique_ptr<MergeSource>> sources;
  for (SSTableReader* reader : inputs)
    sources.push_back(std::make_unique<SSTableSource>(reader->iterate()));

  MergeIterator merge(std::move(sources));
  SSTableWriter writer(out_path);
  Key key;
  ValueEntry entry;
  while (merge.next(key, entry, /*skip_tombstones=*/true)) writer.add(key, entry);
  return writer.finish();
}

}  // namespace minidb
