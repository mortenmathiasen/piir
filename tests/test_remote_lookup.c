#include <stdlib.h>
#include "unittest.h"
#include "remotes.h"

#define REMOTES FIXTURE_DIR "/conf/remotes"
#define MISSING FIXTURE_DIR "/no/such/dir"

static void test_list(void) {
  const char *dirs[] = {REMOTES};
  remoteListT list;
  listRemotes(dirs, 1, &list);
  CHECK_EQ_INT(3, list.count);
  if (list.count == 3) {
    CHECK_EQ_STR("brand1/dup", list.names[0]);
    CHECK_EQ_STR("brand1/only", list.names[1]);
    CHECK_EQ_STR("brand2/dup", list.names[2]);
  }
  freeRemoteList(&list);
}

static void test_list_merges_directories(void) {
  const char *dirs[] = {REMOTES, MISSING, NULL, REMOTES};
  remoteListT list;
  listRemotes(dirs, 4, &list);
  CHECK_EQ_INT(3, list.count);
  freeRemoteList(&list);
}

static void test_find_full_name(void) {
  const char *dirs[] = {MISSING, REMOTES};
  char *path = NULL;
  CHECK_EQ_INT(1, findRemote(dirs, 2, "brand2/dup", &path, NULL));
  CHECK_EQ_STR(REMOTES "/brand2/dup.json", path);
  free(path);
}

static void test_find_unique_file_name(void) {
  const char *dirs[] = {REMOTES};
  char *path = NULL;
  remoteListT matches;
  CHECK_EQ_INT(1, findRemote(dirs, 1, "only", &path, &matches));
  CHECK_EQ_STR(REMOTES "/brand1/only.json", path);
  CHECK_EQ_INT(0, matches.count);
  free(path);
}

static void test_find_ambiguous(void) {
  const char *dirs[] = {REMOTES};
  char *path = NULL;
  remoteListT matches;
  CHECK_EQ_INT(2, findRemote(dirs, 1, "dup", &path, &matches));
  CHECK(path == NULL);
  CHECK_EQ_INT(2, matches.count);
  if (matches.count == 2) {
    CHECK_EQ_STR("brand1/dup", matches.names[0]);
    CHECK_EQ_STR("brand2/dup", matches.names[1]);
  }
  freeRemoteList(&matches);
}

static void test_find_missing(void) {
  const char *dirs[] = {REMOTES};
  char *path = (char *)1;
  CHECK_EQ_INT(0, findRemote(dirs, 1, "nothing", &path, NULL));
  CHECK(path == NULL);
  CHECK_EQ_INT(0, findRemote(dirs, 1, "brand1/nothing", &path, NULL));
  CHECK_EQ_INT(0, findRemote(dirs, 1, "", &path, NULL));
  CHECK_EQ_INT(0, findRemote(dirs, 1, "README", &path, NULL));
  // No escaping the remote directories
  CHECK_EQ_INT(0, findRemote(dirs, 1, "../conf/remotes/brand1/only", &path, NULL));
}

int main(void) {
  RUN_TEST(test_list);
  RUN_TEST(test_list_merges_directories);
  RUN_TEST(test_find_full_name);
  RUN_TEST(test_find_unique_file_name);
  RUN_TEST(test_find_ambiguous);
  RUN_TEST(test_find_missing);
  return TEST_RESULT();
}
