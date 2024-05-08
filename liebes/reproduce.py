import shutil
import subprocess
from pathlib import Path

from datetime import datetime
import argparse
import time
import os


class ReproUtil:
    def __init__(self, home_dir, fs_image_root, port, linux_dir, ltp_dir, pid_index=0, vm_mem=10):
        self.home_dir = home_dir
        self.kernel_image_path = None
        self.work_linux_dir = None
        self.fs_image_root = self.home_dir + "/" + fs_image_root
        self.fs_image_path = self.fs_image_root + "/bullseye.img"
        self.ssh_key = self.fs_image_root + "/bullseye.id_rsa"
        self.pid_index = pid_index
        self.pid_file = self.home_dir + f"/vm_{pid_index}.pid"
        self.vm_port = port
        # for copy only
        self.base_linux_dir = self.home_dir + "/" + linux_dir

        self.result_dir = None
        self.ltp_dir = self.home_dir + "/" + ltp_dir
        self.ltp_bin = None
        self.ltp_version = None
        self.vm_mem = vm_mem
        self.vm_ltp_bin = "/root/ltp_bin"
        self.vm_selftest_bin = "/root/selftest_bin"
        self.selftest_bin = None
        self.qemu_process = None
        pass

    def run_command(self, cmd, cwd=None):
        if cwd is None:
            cwd = self.home_dir
        print(f"execute command: {cmd}")
        result = subprocess.run(cmd, shell=True, capture_output=True, text=True, cwd=cwd)
        if result.returncode != 0:
            print(f"execute command failed: {cmd}")
            print(f"error message: {result.stderr}")
        return result

    def prepare_selftest_binary(self):
        '''
        some necessary packages are required to build selftest
        sudo apt install libpopt-dev
        sudo apt install libasound2-dev
        sudo apt install libcap-ng-dev
        sudo apt install libfuse-dev
        '''

        if self.work_linux_dir is None:
            print("linux image is not prepared")
            return False
        if (self.work_linux_dir / "selftest_build").exists():
            self.selftest_bin = self.work_linux_dir / "selftest_build"
            print(f"selftest bin is located at {self.selftest_bin.absolute()}")
            return True

        cmd = "mkdir selftest_build"
        if self.run_command(cmd, cwd=self.work_linux_dir).returncode != 0:
            return False
        cmd = "make headers"
        if self.run_command(cmd, cwd=self.work_linux_dir).returncode != 0:
            return False
        cmd = f"make -C tools/testing/selftests install INSTALL_PATH={(self.work_linux_dir / 'selftest_build').absolute()}"
        if self.run_command(cmd, cwd=self.work_linux_dir).returncode != 0:
            return False
        self.selftest_bin = self.work_linux_dir / "selftest_build"
        print(f"selftest bin is located at {self.selftest_bin.absolute()}")
        return True

    def prepare_ltp_binary(self, git_sha=r'tmp'):
        # make sure all dependencies are installed
        # check the result of ./configure in ltp
        # if some dependencies are missing, install them

        # step0. update ltp to the latest version
        # cmd = "git pull origin master"
        # if self.run_command(cmd, self.ltp_dir).returncode != 0:
        #     return False

        # step1. checkout ltp to the specific git sha
        self.ltp_version = git_sha
        
        work_ltp_dir = Path(f"{self.home_dir}/repro_ltps/{git_sha}")
        if work_ltp_dir.exists() and (work_ltp_dir / "build").exists():
            self.ltp_bin = work_ltp_dir / "build"
            print(f"ltp bin is located at {self.ltp_bin.absolute()}")
            return True
        # work_ltp_dir.mkdir(parents=True)
        cmd = f"cp -r {self.ltp_dir} {work_ltp_dir}"
        if self.run_command(cmd).returncode != 0:
            return False
        # cmd = "git clean -df && git checkout " + git_sha + " && git clean -df"
        cmd = "git clean -df"
        if self.run_command(cmd, work_ltp_dir).returncode != 0:
            return False
        # step2. make whole ltp into binary, the location of the binary is ./build
        cmd = f"make autotools && mkdir build && ./configure --prefix={work_ltp_dir / 'build'}"
        if self.run_command(cmd, work_ltp_dir).returncode != 0:
            return False
        cmd = f"make -k -j`nproc` && make -k install"
        if self.run_command(cmd, work_ltp_dir).returncode != 0:
            return False
        self.ltp_bin = work_ltp_dir / "build"
        print(f"ltp bin is located at {work_ltp_dir / 'build'}")
        return True

    def copy_essential_files_to_vm(self):
        # copy kernel config
        cmd = f"scp -i {self.ssh_key} -P {self.vm_port} -o \"StrictHostKeyChecking no\" -r {self.work_linux_dir}/.config root@localhost:/root/config"
        if self.run_command(cmd).returncode != 0:
            return False

        cmd = f"gzip -f config "
        if self.execute_cmd_in_qemu(cmd).returncode != 0:
            return False

        # copy kernel modules
        cmd = f"mkdir -p /lib/modules/\$(uname -r)/"
        if self.execute_cmd_in_qemu(cmd).returncode != 0:
            return False
        cmd = f"scp -i {self.ssh_key} -P {self.vm_port} -o \"StrictHostKeyChecking no\" -r {self.work_linux_dir}/modules.builtin \"root@localhost:/lib/modules/\$(uname -r)/\""
        if self.run_command(cmd).returncode != 0:
            return False

        # need to determine the gcov path first. :(
        gcov_path = f"/sys/kernel/debug/gcov{self.work_linux_dir}"
        cmd = f"ls {gcov_path}"
        res = self.execute_cmd_in_qemu(cmd)
        if res.returncode != 0:
            if "/data" in gcov_path:
                gcov_path = gcov_path.replace("/data", "/home")
            elif "/home" in gcov_path:
                gcov_path = gcov_path.replace("/home", "/data")
            cmd = f"ls {gcov_path}"
            res = self.execute_cmd_in_qemu(cmd)
            if res.returncode != 0:
                print(f"failed to determine gcov path: {self.work_linux_dir}")

        # copy coverage collecting script to vm
        shell_content = f'''#!/bin/bash -e

        DEST=$1
        GCDA={gcov_path}

        if [ -z "$DEST" ] ; then
            echo "Usage: $0 <output.tar.gz>" >&2
            exit 1
        fi

        TEMPDIR=$(mktemp -d)
        echo Collecting data..
        find $GCDA -type d -exec mkdir -p $TEMPDIR/\\{{\\}} \\;
        find $GCDA -name '*.gcda' -exec sh -c 'cat < $0 > '$TEMPDIR'/$0' {{}} \\;
        # find $GCDA -name '*.gcno' -exec sh -c 'cp -d $0 '$TEMPDIR'/$0' {{}} \\;
        tar czf $DEST -C $TEMPDIR/$GCDA .
        rm -rf $TEMPDIR

        echo "$DEST successfully created, copy to build system and unpack with:"
        echo "  tar xfz $DEST "
                '''
        script_file = Path(self.work_linux_dir) / "collect_coverage.sh"
        script_file.write_text(shell_content)
        script_file.chmod(0o755)
        cmd = f"scp -i {self.ssh_key} -P {self.vm_port} -o \"StrictHostKeyChecking no\" {script_file.absolute()} root@localhost:/root/collect_coverage.sh"
        if self.run_command(cmd).returncode != 0:
            return False

    def copy_ltp_bin_to_vm(self):
        # check if the ltp binary is already there
        cmd = "cat /root/ltp_bin/Version"
        res = self.execute_cmd_in_qemu(cmd)
        if res.returncode == 0 and res.stdout.strip() == self.ltp_version:
            print(f"{res.stdout.strip()}")
            print(f"ltp binary is already in the vm, tag version: {self.ltp_version}")
            return True

        cmd = f"rm -rf {self.vm_ltp_bin}"
        if self.execute_cmd_in_qemu(cmd).returncode != 0:
            return False
        cmd = f"scp -i {self.ssh_key} -P {self.vm_port} -o \"StrictHostKeyChecking no\" -r {self.ltp_bin} root@localhost:/root/ltp_bin"
        if self.run_command(cmd).returncode != 0:
            return False
        return True

    def copy_selftest_bin_to_vm(self):
        cmd = f"rm -rf {self.vm_selftest_bin}"
        if self.execute_cmd_in_qemu(cmd).returncode != 0:
            return False
        cmd = f"scp -i {self.ssh_key} -P {self.vm_port} -o \"StrictHostKeyChecking no\" -r {self.selftest_bin} root@localhost:/root/selftest_bin"
        if self.run_command(cmd).returncode != 0:
            return False
        return True

    def prepare_linux_image(self, git_sha, base_config, extra_config, renew=False):
        work_linux_dir = Path(f"{self.home_dir}/repro_linuxs/{git_sha}")
        extra_config = extra_config + r' CONFIG_KCOV=y CONFIG_DEBUG_INFO_DWARF4=y CONFIG_KASAN=y CONFIG_KASAN_INLINE=y CONFIG_CONFIGFS_FS=y CONFIG_SECURITYFS=y'
        self.work_linux_dir = work_linux_dir
        self.kernel_image_path = work_linux_dir / "arch/x86/boot/bzImage"
        self.result_dir = f"{self.home_dir}/repro_results/{git_sha}"
        if not Path(self.result_dir).exists():
            Path(self.result_dir).mkdir()
        cmd = f"ssh-keygen -R [localhost]:{self.vm_port}"
        self.run_command(cmd)
        if Path(self.kernel_image_path).exists():
            if not renew:
                return True
            shutil.rmtree(work_linux_dir.absolute())
        # cp a new one
        cmd = f"cp -r {self.base_linux_dir} {work_linux_dir}"
        if self.run_command(cmd).returncode != 0:
            return False
        # checkout to the git sha
        cmd = "git checkout " + git_sha
        if self.run_command(cmd, work_linux_dir).returncode != 0:
            return False
        # clean the git repo
        cmd = "git clean -df"
        if self.run_command(cmd, work_linux_dir).returncode != 0:
            return False
        cmd = "make mrproper"
        if not self.run_command(cmd, work_linux_dir):
            return False
        # generate config
        cmd = "make " + base_config
        if self.run_command(cmd, work_linux_dir).returncode != 0:
            return False
        cmd = "make kvm_guest.config"
        if self.run_command(cmd, work_linux_dir).returncode != 0:
            return False
        Path(f"{work_linux_dir}/kernel/configs/temp.config").write_text(
            "\n".join(extra_config.split(" ")))
        cmd = "make temp.config"
        if self.run_command(cmd, work_linux_dir).returncode != 0:
            return False
        # make image
        cmd = "make -j`nproc`"
        if self.run_command(cmd, work_linux_dir).returncode != 0:
            return False

        # check the image is generated
        image_path = f"{work_linux_dir}/arch/x86/boot/bzImage"
        if not Path(image_path).exists():
            return False
        cmd = "rm -rf .git"
        self.run_command(cmd, work_linux_dir)
        return True

    def start_qemu(self):
        # kill the previous qemu process if exists
        if Path(self.pid_file).exists():
            pid = Path(self.pid_file).read_text().strip()
            cmd = f"kill -9 {pid}"
            self.run_command(cmd)
            Path(self.pid_file).unlink()

        cmd = f'qemu-system-x86_64 \
        -m {self.vm_mem}G \
        -smp 2 \
        -kernel {self.kernel_image_path} \
        -append "console=ttyS0 root=/dev/sda earlyprintk=serial net.ifnames=0" \
        -drive file={self.fs_image_path},format=raw \
        -net user,host=10.0.2.10,hostfwd=tcp:127.0.0.1:{self.vm_port}-:22 \
        -net nic,model=e1000 \
        -enable-kvm \
        -nographic \
        -pidfile {self.pid_file} \
        2>&1 | tee vm_{self.pid_index}.log'
        print(cmd)
        # vm_log_file = Path(self.result_dir) / "vm.log"
        # qemu_process = subprocess.Popen(cmd, shell=True, text=True, stdout=vm_log_file.open("w"),
        #                                 stderr=vm_log_file.open("w"))
        qemu_process = subprocess.Popen(cmd, shell=True, text=True)
        self.qemu_process = qemu_process
        return

    def execute_cmd_in_qemu(self, cmd):
        cmd = f'ssh -i {self.ssh_key} -p {self.vm_port} -o "StrictHostKeyChecking no" -t root@localhost "{cmd}"'
        res = self.run_command(cmd)
        return res

    def collect_coverage(self):
        pass

    def execute_testcase(self, testcase_name, testcase_type, timeout=30, save_result=True, collect_coverage=False):
        if collect_coverage:
            cmd = f'lcov -z'
            res = self.execute_cmd_in_qemu(cmd)
            if res.returncode != 0:
                print("failed to reset lcov")
                return
        # collect time cost
        start_time = datetime.now()
        if testcase_type == "ltp":
            cmd = f'cd /root/ltp_bin && LTP_TIMEOUT_MUL=5 KCONFIG_PATH=/root/config.gz timeout {timeout}s ./runltp -s {testcase_name}'
        elif testcase_type == "selftest":
            cmd = f'cd /root/selftest_bin && timeout {timeout}s ./run_kselftest.sh -t {testcase_name}'
        else:
            print(f"testcase type {testcase_type} not supported, use [ltp | selftest] instead")
            return
        res = self.execute_cmd_in_qemu(cmd)
        cost_time = datetime.now() - start_time
        print(f"test case {testcase_name} cost time: {cost_time}")
        if save_result:
            if res.returncode != 0:
                if res.returncode == 124:
                    print("timeout")
                    result_file = Path(self.result_dir) / f"{testcase_name}.err"
                    result_file.write_text(res.stdout + res.stderr + f"\nTime cost: {cost_time}s\n")
                    print(f"save err result to {result_file}")
                else:
                    result_file = Path(self.result_dir) / f"{testcase_name}.result"
                    result_file.write_text(res.stdout + res.stderr + f"\nTime cost: {cost_time}s\n")
                    print(f"save fail result to {result_file}")
            else:
                result_file = Path(self.result_dir) / f"{testcase_name}.result"
                result_file.write_text(res.stdout + f"\nTime cost: {cost_time}s\n")
                print(f"save succ result to {result_file}")
        if collect_coverage:
            if Path(f"{self.work_linux_dir}/{testcase_name}_coverage.tar.gz").exists():
                print(f"coverage file {testcase_name}_coverage.tar.gz already exists")
                return
            cmd = f'cd /root && ./collect_coverage.sh /root/{testcase_name}_coverage.tar.gz'
            res = self.execute_cmd_in_qemu(cmd)
            if res.returncode != 0:
                print("failed to collect coverage")
                return
            cmd = (
                f'scp -i {self.ssh_key} -P {self.vm_port} -o "StrictHostKeyChecking no" root@localhost:/root/{testcase_name}_coverage.tar.gz {self.work_linux_dir}/{testcase_name}_coverage.tar.gz')
            res = self.run_command(cmd)
            if res.returncode != 0:
                print("failed to copy coverage")
                return
            cmd = f"cd /root && rm -f /root/{testcase_name}_coverage.tar.gz"
            self.execute_cmd_in_qemu(cmd)

    def kill_qemu(self):
        pid_file = Path(self.pid_file)
        if pid_file.exists():
            pid = pid_file.read_text().strip()
            cmd = f"kill -9 {pid}"
            self.run_command(cmd)
            pid_file.unlink()

    def parse_result(self):
        pass

    def save_result(self):
        pass


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    # python temp.py --vmport=8001 --vmindex=1 --vmnum=15 --file_image=file_images/ltp-1 --ltp_repo=ltp_mirror --linux_repo=linux --home_dir=/home/userpath
    # Add arguments
    parser.add_argument("--vmport", type=int, default=8001, help="vm port, range from 8001-8009")
    parser.add_argument("--vmindex", type=int, default=1, help="vm index, range from 1-9")

    parser.add_argument("--file_image", type=str, default="kernel_images/ltp",
                        help="relevant path of the file image from home dir")
    parser.add_argument("--ltp_repo", type=str, default="ltp-cp-only",
                        help="relevant path of the ltp repo from home dir")
    parser.add_argument("--linux_repo", type=str, default="linux", help="relevant path of the linux repo from home dir")
    parser.add_argument("--home_dir", type=str, default="/home/userpath", help="home dir")
    parser.add_argument("--config", type=str, default="", help="linux build config")
    parser.add_argument("--git_sha", type=str, help="linux git version")
    parser.add_argument("--tc_name", type=str, help="test case name")
    parser.add_argument("--test_suite", type=str, help="choose the test suite: ltp or selftest")
    args = parser.parse_args()
    print(args)

    logger_file_name = r'reproduce.log'
    pid = os.getpid()
    pid_root = Path("pids")
    if not pid_root.exists():
        pid_root.mkdir(parents=True)
    (pid_root / f"{args.vmindex}.pid").write_text(str(pid))
    (pid_root / f"{args.vmindex}.log").write_text(logger_file_name)



    repro_root = Path("/data/userpath/repro_results")

    config_cmd = args.config
    base_config = "defconfig"
    reproutil = ReproUtil(
        home_dir=args.home_dir,
        fs_image_root=args.file_image,
        port=args.vmport,
        linux_dir=args.linux_repo,
        ltp_dir=args.ltp_repo,
        pid_index=args.vmindex,
    )
    res = reproutil.prepare_linux_image(
        git_sha=args.git_sha,
        base_config=base_config,
        extra_config=config_cmd,
        # renew=True
    )
    if not res:
        print("failed to prepare linux image")
        exit(-1)
    res = reproutil.prepare_ltp_binary()
    if not res:
        print("failed to prepare ltp binary")
        exit(-1)
    res = reproutil.prepare_selftest_binary()
    if not res:
        print("failed to prepare selftest binary")
        exit(-1)

    reproutil.start_qemu()
    print("start qemu vm")
    time.sleep(20)
    print("copy ltp to qemu")
    reproutil.copy_essential_files_to_vm()
    reproutil.copy_ltp_bin_to_vm()
    print("copy selftest to qemu")
    reproutil.copy_selftest_bin_to_vm()
    print("start to execute testcases")
    
    reproutil.execute_testcase(args.tc_name, args.test_suite, 600, collect_coverage=False)

    print("done")
    reproutil.kill_qemu()

