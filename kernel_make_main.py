from test import *

filtered_results = []
with open(r'checkout_with_failed_cases.txt') as f:
    for git_sha in f:
        filtered_results.append(git_sha[: -1])
f.close()

target_list = filtered_results[40: ]
kernel_list_delete(target_list)
# kernel_list_separation(target_list)
# kernel_list_make_config(target_list)
# generate_list_build_sh(target_list)
# kernel_list_bc_gen(target_list)
# kernel_list_cg_gen(target_list)
print('continue run...')