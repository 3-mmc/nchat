// Draft durability, account isolation, Unicode and malformed-file recovery.
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>
#include "uidrafts.h"
#include "unittest.h"

int main()
{
  char temp[] = "/tmp/nchat-drafts-test-XXXXXX";
  const char* dir = mkdtemp(temp);
  if (!dir) return 1;
  const std::string path = std::string(dir) + "/drafts.txt";
  try
  {
    UiDrafts::Store drafts = {
      {{"Signal_one", "same-id"}, {L"hello\n世界 👋\tend", 3}},
      {{"WhatsApp_two", "same-id"}, {L"other account", 13}}
    };
    unittest::ExpectTrue(UiDrafts::Load(path).empty());
    unittest::ExpectTrue(UiDrafts::Save(path, drafts));
    unittest::ExpectTrue(UiDrafts::Load(path) == drafts);
    struct stat info;
    unittest::ExpectTrue(stat(path.c_str(), &info) == 0);
    unittest::ExpectEqual(int, info.st_mode & 0777, 0600);
    // Invalid rows never discard intact neighbouring drafts.
    std::ofstream out(path, std::ios::app);
    out << "bad record\nzz 00 3 ab\n61 62 999 6869\n63 64 -8 6869\n";
    out.close();
    auto loaded = UiDrafts::Load(path);
    unittest::ExpectTrue(loaded.at({"Signal_one", "same-id"}) == drafts.at({"Signal_one", "same-id"}));
    unittest::ExpectEqual(int, loaded.at({"a", "b"}).second, 2);
    unittest::ExpectEqual(int, loaded.at({"c", "d"}).second, 0);
    unittest::ExpectFalse(UiDrafts::Save(path + "/missing", drafts));
    unittest::ExpectTrue(UiDrafts::Load(path) == loaded); // failed write preserves saved file
    drafts.erase({"Signal_one", "same-id"});
    unittest::ExpectTrue(UiDrafts::Save(path, drafts));
    unittest::ExpectTrue(UiDrafts::Load(path) == drafts);
    unittest::ExpectTrue(UiDrafts::Save(path, {}));
    unittest::ExpectTrue(UiDrafts::Load(path).empty());
    unlink(path.c_str());
    rmdir(dir);
    std::cout << "PASS: persistent drafts\n";
    return 0;
  }
  catch (const std::exception& error)
  {
    unlink(path.c_str());
    rmdir(dir);
    std::cerr << error.what() << '\n';
    return 1;
  }
}
