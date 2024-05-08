import math
import random
import pathlib
from liebes.CiObjects import *
import numpy as np
from liebes.analysis import CIAnalysis
from liebes.EHelper import EHelper
from liebes.sql_helper import SQLHelper
from liebes.ci_logger import logger
from liebes.tcp_approach import cg_based
import multiprocessing as mp


def do_exp3(params):
    cg_based.do_exp3(params[0], params[1], params[2])

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

    strategies = [
        'total',
        'additional'
    ]

    gap_versions = [
        0
    ]

    context_strategy = "default"
    # tokenizer = AstTokenizer()
    # ir_model = TfIdfModel()
    # TODO 加个多线程的方式
    summary = []
    params_list = []
    res = cg_based.do_exp4('additional')
    # for strategy in strategies:
        # params_list.append((r'/home/userpath/projects/kernel-tcp/logs/flaky_info.txt', strategy, 0))
        # res = cg_based.do_exp4('additional')
            # summary.append(res)
    # logger.info("summary :---------------------------------")
    # for s in summary:
        # logger.info(s)
    
    # pool = mp.Pool()
    # pool.map(do_exp3, params_list)
    # pool.close()
    # pool.join()

    # for strategy in strategies:
    #     res = cg_based.do_exp2(r'/home/userpath/projects/kernel-tcp/logs/main-2024-03-29-16:21:44.txt', strategy)
    #     summary.append(res)
    # logger.info("summary :---------------------------------")
    # for s in summary:
    #     logger.info(s)