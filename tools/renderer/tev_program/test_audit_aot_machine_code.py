import unittest
from audit_aot_machine_code import count_instructions


class MachineCodeTests(unittest.TestCase):
    def test_static_categories_and_unresolved_calls(self):
        text = '''header
180001000: mov eax, dword ptr [rbx + 0x8]
180001003: lea rcx, [rbp - 0x10]
180001007: call 0x180002000 <name>
18000100c: call rax
18000100e: jne 0x180001000
'''
        r = count_instructions(text, {0x2000: '?ReadFast@x'}, 0x180000000)
        self.assertEqual(r['static_counts'], {'instructions': 5,
            'memory_operand_instructions_including_lea': 2,
            'call_sites': 2, 'branch_sites': 1})
        self.assertEqual(r['call_site_categories'], {
            'memory_read_helper': 1, 'other_or_unresolved': 1})

    def test_empty_output_has_no_counts(self):
        self.assertEqual(count_instructions('header', {}, 0)['static_counts'], {})


if __name__ == '__main__':
    unittest.main()
