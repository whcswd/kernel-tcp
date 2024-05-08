from typing import List
import json
import re


class EHelper:
    def __init__(self):
        self.tokenizers = []
        self.ir_model = []

    def APFD(self, total_faults: List, test_cases_order: List) -> float:
        if len(total_faults) == 0 or len(test_cases_order) == 0:
            return 0.5
        res = 0.0
        for i in range(len(test_cases_order)):
            if test_cases_order[i] in total_faults:
                res += i + 1
        res /= (len(total_faults) * len(test_cases_order))
        res = 1 - res
        res += 1 / (2 * len(test_cases_order))
        return res

    def get_ltp_version(self, git_sha: str):
        with open(r'checkout_ltp_version_map.json', r'r') as f:
            data = json.load(f)
        f.close()

        return data.get(git_sha)
    
    def get_checkout_info(self):
        lines = []
        with open(r'/home/userpath/projects/kernel-tcp/logs/flaky_info.txt', r'r') as f:
            for line in f:
                lines.append(line)
        f.close()

        history_infos = []
        for idx in range(len(lines)):
            cur_line = lines[idx]
            if r'checkout' in cur_line:
                commit_match = re.search(r'checkout: (.*?)\n', cur_line)
                git_sha = commit_match.group(1)
                next_line = lines[idx + 1]
                total_cases_match = re.search(r'total_cases: \[(.*?)\]', next_line)
                total_cases_tmp = total_cases_match.group(1).split(', ')
                total_cases_list = []
                for tct in total_cases_tmp:
                    total_cases_list.append(tct[1: -1])
                next_2_line = lines[idx + 2]
                fault_match = re.search(r'fail_cases: \[(.*?)\]', next_2_line)
                fault_tmp = fault_match.group(1).split(', ')
                fault_list = []
                for ft in fault_tmp:
                    fault_list.append(ft[1: -1])
                history_infos.append((git_sha, total_cases_list, fault_list))
        
        return history_infos


