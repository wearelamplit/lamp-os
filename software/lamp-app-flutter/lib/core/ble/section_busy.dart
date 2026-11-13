/// True for what a lamp serves in place of a config section (`lamp`, `base`,
/// `shade`, `expr`, `home`) it lacks the heap to build or copy out: the single
/// byte `!`. It only ever reaches a link that has authenticated.
bool isSectionBusy(List<int> bytes) => bytes.length == 1 && bytes[0] == 0x21;
