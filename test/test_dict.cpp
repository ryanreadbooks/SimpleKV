#include <unordered_map>
#include <iostream>
#include "../src/hashdict.h"

using namespace std;

int main() {

  auto displayStrings = [](const vector<DynamicString> &strings) {
    for (size_t i = 0; i < strings.size(); ++i) {
      if (i != strings.size() - 1) {
        cout << strings[i] << ", ";
      } else {
        cout << strings[i] << endl;
      }
    }
  };

  auto displayHashTable = [&](const HashTable &ht) {
    cout << "-----------------------\n";
    cout << "HashTable count = " << ht.Count() << ", load factor = "
         << ht.LoadFactor() << endl;
    cout << "+ All keys are:   ";
    displayStrings(ht.AllKeys());
    cout << "+ All values are: ";
    displayStrings(ht.AllValues());
    cout << "-----------------------\n";
  };

  cout << "sizeof(HTEntry) = " << sizeof(HTEntry) << endl;
  cout << "sizeof(HashTable) = " << sizeof(HashTable) << endl;
  cout << "sizeof(unordered_map) = " << sizeof(unordered_map<int, int>) << endl;
  cout << "sizeof(DynamicString) = " << sizeof(DynamicString) << endl;

  cout << "============== Test HashTable ==============\n";

  HashTable hashtable;
  hashtable.UpdateKV(HEntryKey("k1", 2), HEntryVal("v1", 2));
  hashtable.UpdateKV(HEntryKey("k2", 2), HEntryVal("v2", 2));
  hashtable.UpdateKV(HEntryKey("k3", 2), HEntryVal("v3", 2));
  hashtable.UpdateKV(HEntryKey("k4", 2), HEntryVal("v4", 2));
  hashtable.UpdateKV(HEntryKey("k5", 2), HEntryVal("v5", 2));

  std::vector<HEntryKey> keys = hashtable.AllKeys();
  std::vector<HEntryKey> values = hashtable.AllValues();

  displayHashTable(hashtable);

  cout << "Removing key = k3... \n";
  cout << hashtable.EraseKey(HEntryKey("k3", 2)) << endl;
  displayHashTable(hashtable);

  cout << "Updating key = k2 ... \n";
  cout << hashtable.UpdateKV("k2", "heo") << endl;
  displayHashTable(hashtable);

  try {
    hashtable.At("k3");
  } catch (std::out_of_range &ex) {
    cout << ex.what() << endl;
    cout << "k2 = " << hashtable.At("k2") << endl;
  }

  cout << "has key k44 ? " << boolalpha << hashtable.CheckExists("k44") << endl;
  cout << "has key k1 ? " << boolalpha << hashtable.CheckExists("k1") << endl;
  cout << "Erase a non-existing key k99, res = " << hashtable.EraseKey("k99") << endl;

  int n = 10;
  cout << "inserting " << n << " keys ...\n";
  for (int i = 0; i < 10; ++i) {
    hashtable.UpdateKV(HEntryKey(to_string(i)), HEntryVal(to_string(i)));
  }
  displayHashTable(hashtable);

  cout << "============== Test HashTable End ==============\n\n";

  cout << "============== Test HashDict ==============\n";

  auto displayDict = [&] (const HashDict& d) {
    cout << "-----------------------\n";
    cout << "HashDict item count = " << d.Count() << endl;
    cout << "+ All keys are:   ";
    displayStrings(d.AllKeys());
    cout << "+ All values are: ";
    displayStrings(d.AllValues());
    cout << "-----------------------\n";
  };

  HashDict dict;
  n = 100;
  for (int i = 0; i < n; ++i) {
    dict.Update(std::to_string(i), std::to_string(i));
  }
  displayDict(dict);

  cout << "now erase some key-value... \n";
  n = 80;
  srand(time(nullptr));
  srandom(time(nullptr));
  int n_key_erased = 0;
  for (int i = 0; i < n; ++i) {
    string key = to_string(rand() % dict.Count());
    bool ans = dict.Erase(key) == ERASED;
    if (ans) ++n_key_erased;
    cout << "key -> " << key << (ans ? " erased" : " not exists") << endl;
  }
  cout << "try to erase " << n << " items, finally erased " << n_key_erased << " items\n";

  displayDict(dict);

  cout << "random access some items...\n";
  n = 60;
  srand(time(nullptr));
  srandom(time(nullptr));
  for (int i = 0; i < n; ++i) {
    string key = to_string(rand() % dict.Count());
    try {
      cout << "key = " << key << " : value = " << dict.At(key) << endl;
    } catch (std::out_of_range& ex) {
      cout << ex.what() << endl;
    }
  }
  dict.Update("tv1", "video");
  cout << dict.At("tv1") << endl;
  dict.At("tv1") = HEntryVal("game");
  cout << dict.At("tv1") << endl;

  cout << "============== Test HashDict End ==============\n";

  return 0;
}