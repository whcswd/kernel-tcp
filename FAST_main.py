import math
import random
import pathlib
from liebes.CiObjects import *
import numpy as np
from liebes.analysis import CIAnalysis
from liebes.EHelper import EHelper
from liebes.sql_helper import SQLHelper
from liebes.ci_logger import logger
from liebes.tcp_approach import FAST

if __name__ == '__main__':
    # linux_path = '/home/userpath/linux-1'
    # sql = SQLHelper()
    # checkouts = sql.session.query(DBCheckout).order_by(DBCheckout.git_commit_datetime.desc()).limit(10).all()
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
    # cia.filter_job("FILTER_NOFILE_CASE")
    # cia.ci_objs = cia.ci_objs[1:]
    # cia.statistic_data()

    
    # tokenizer = AstTokenizer()
    # ir_model = TfIdfModel()
    # TODO 加个多线程的方式
    summary = []

    ltp_version = [
        '20230127',
        '20230516',
        '20230929',
        '20240129'
    ]
    # FAST.do_exp(r'logs/ci_info.txt', 10, 1, 10, 0.9, None)
    for version in ltp_version:
        res = FAST.do_exp(r'logs/ci_info.txt', 10, 1, 10, 0.9, version)
        summary.append(res)
    logger.info("summary :---------------------------------")
    for s in summary:
        logger.info(s)