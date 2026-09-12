import unittest

from verify_storms_learning import verify


class StormsLearningChecks(unittest.TestCase):
    def setUp(self):
        self.before = {"quest_flags": 0x109DD620,
                       "lesson_completion_event": 0x8000}
        self.after = {"quest_flags": 0x109FD620,
                      "lesson_completion_event": 0x8020}

    def test_native_completion(self):
        self.assertTrue(all(verify(self.before, self.after).values()))

    def test_unchanged_save_is_not_learning(self):
        self.assertFalse(all(verify(self.before, self.before).values()))

    def test_already_learned_is_not_first_learning(self):
        self.assertFalse(all(verify(self.after, self.after).values()))

    def test_unrelated_quest_changes_are_rejected(self):
        self.after["quest_flags"] ^= 1
        self.assertFalse(all(verify(self.before, self.after).values()))

    def test_song_flag_without_completion_is_rejected(self):
        self.after["lesson_completion_event"] = 0x8000
        self.assertFalse(all(verify(self.before, self.after).values()))


if __name__ == "__main__":
    unittest.main()
