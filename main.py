import json
from datetime import datetime

from liebes.CiObjects import *
from liebes.EHelper import EHelper
from liebes.GitHelper import GitHelper
from liebes.analysis import CIAnalysis
from liebes.ir_model import *
from liebes.sql_helper import SQLHelper
from liebes.tokenizer import *
from git import Repo
import tree_sitter
from tree_sitter import Language, Parser
import difflib
import subprocess
import numpy as np
import requests
import re
from datetime import datetime
import pymysql
import os
import pathlib

class TestCaseInfo:
    def __init__(self):
        self.file_path = ""
        self.type = ""


def update_token_mapping(m, mapping_path):
    json.dump(m, Path(mapping_path).open("w"))


def read_json(path):
    text = pathlib.Path(path).read_text()
    text = text.strip()
    text = text.removeprefix("{")
    text = text.removesuffix("}")
    text = re.sub("\n\s*", "", text)
    objs = text.split("}{")
    json_obj_list = []
    for obj in objs:
        d = json.loads("{" + obj + "}")
        json_obj_list.append(d)
    return json_obj_list


def get_test_name(data_dir, git_sha, test_id):
    file_path = f'/home/userpath/projects/{data_dir}/linux-mainlin/{git_sha}/test.json'
    if not os.path.exists(file_path):
        return None
    
    test_list = read_json(file_path)
    for t in test_list:
        if str(t['id']) == test_id:
            return t['name']
        
    return None
    

if __name__ == '__main__':
    # sql = SQLHelper()
    # checkouts = sql.session.query(DBCheckout).order_by(DBCheckout.git_commit_datetime.desc()).limit(600).all()
    # cia = CIAnalysis()
    # for ch in checkouts:
    #     cia.ci_objs.append(Checkout(ch))
    # cia.reorder()
    # cia.set_parallel_number(40)
    # cia.filter_job("COMBINE_SAME_CASE")
    # cia.filter_job("FILTER_SMALL_BRANCH", minimal_testcases=20)
    # cia.filter_job("COMBINE_SAME_CONFIG")
    # cia.filter_job("CHOOSE_ONE_BUILD")
    # cia.filter_job("FILTER_SMALL_BRANCH", minimal_testcases=20)
    # cia.filter_job("FILTER_FAIL_CASES_IN_LAST_VERSION")
    # cia.statistic_data()

    # for ci_obj in cia.ci_objs:
    #     test_cases = ci_obj.get_all_testcases()
    #     total_cases = []
    #     fail_cases = []
    #     for t in test_cases:
    #         total_cases.append(t.file_path)
    #         if t.is_failed():
    #             fail_cases.append(t.file_path)

    #     logger.info(f'checkout: {ci_obj.instance.git_sha}')
        # logger.info(f'total_cases: {total_cases}')
        # logger.info(f'fail_cases: {fail_cases}')


    lines = []
    with open(r'logs/flaky_info.txt', r'r') as f:
        for line in f:
            lines.append(line)
    f.close()

    ehelper = EHelper()
    history_infos = ehelper.get_checkout_info()

    fail_cases_set = set()
    for hi in history_infos:
        for f in hi[2]:
            if r'.sh' in f:
                fail_cases_set.add(f)
    
    print(fail_cases_set)
    # for fr in filtered_results:
    #     if fr not in checkout_set:
    #         print(fr)

    # for hi in history_infos:
    #     if hi[0] in filtered_results:
    #         logger.info(f'checkout: {hi[0]}')
    #         logger.info(f'total_cases: {hi[1]}')
    #         logger.info(f'fail_cases: {hi[2]}')