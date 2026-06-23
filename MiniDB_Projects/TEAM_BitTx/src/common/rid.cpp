#include "common/rid.h"

#include <sstream>

namespace minidb {

using namespace std;

string RecordId::ToString() const {
  ostringstream oss;
  oss << "RID(" << pageId_ << "," << slotNum_ << ")";
  return oss.str();
}

} // namespace minidb
