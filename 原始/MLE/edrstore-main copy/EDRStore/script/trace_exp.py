import sys
import getopt
import os

program = "./ClientMain"

def Usage():
    print("{name} -i [the trace folder] -o [output script name] -t [u/d] -m [method type]"
        " -c [client id] -d [download root dir] -a [batch size] -b [start client id]"
        " -s [keep restore file] -f [using ramdisk] -r [ramdisk path]".format(name=__file__))
    print("-t: operation ([u/d]):")
    print("\tu: upload")
    print("\td: download")
    print("-m: method type:")
    print("\t0: only enc mode")
    print("\t1: comp + enc mode")
    print("\t2: full-edr mode")
    print("-c: client id, fixed when -a not set (optional, default: 1)")
    print("-a: batch size, every N commands client id +1 (optional, default: fixed -c)")
    print("-b: start client id when -a is set (optional, default: 1)")
    print("-d: download root dir, -o passed to ClientMain = root/dataset_name")
    print("-s: restore files [keep restore files]")
    print("\t0: not keep restore files")
    print("\t1: keep restore files")
    print("-f: use ramdisk? [using ramdisk]")
    print("\t0: do not use ramdisk")
    print("\t1: use ramdisk")
    print("-r: ramdisk path (required if -f 1)")

def GetAllTrace(folder_name: str):
    file_list = sorted(os.listdir(folder_name))
    return file_list

def GetClientId(idx: int, batch_size: int, start_cid: int, fixed_cid: str):
    """根据批次计算 client ID，-a 未启用时返回固定值"""
    if batch_size > 0:
        return str(start_cid + (idx // batch_size))
    return fixed_cid

if __name__ == "__main__":
    input_folder = "/data/shared_datasets/tar-combine"
    output_script_name = "tar-combine_down.sh"
    operation_type = "d"
    method_type = "2"
    fixed_client_id = "1"
    batch_size = 30          # 0 = 不启用批次轮转
    start_client_id = 1
    download_root = "/home/user2/数据/down"      # -d 参数
    restore_file_stat = 0
    using_ramdisk = 0
    ramdisk_path = ""
    opts, args = getopt.getopt(sys.argv[1:], "i:o:t:m:c:a:b:d:s:f:r:h")
    for opt_name, opt_value in opts:
        if opt_name == '-i':
            input_folder = opt_value
        elif opt_name == '-o':
            output_script_name = opt_value
        elif opt_name == '-t':
            operation_type = opt_value
        elif opt_name == '-m':
            method_type = opt_value
        elif opt_name == '-c':
            fixed_client_id = opt_value
        elif opt_name == '-a':
            batch_size = int(opt_value)
        elif opt_name == '-b':
            start_client_id = int(opt_value)
        elif opt_name == '-d':
            download_root = opt_value
        elif opt_name == '-s':
            restore_file_stat = int(opt_value)
            if (restore_file_stat > 1):
                print("wrong [keep restore files] flag")
                Usage()
                exit()
        elif opt_name == '-f':
            using_ramdisk = int(opt_value)
            if (using_ramdisk > 1):
                print("wrong [using ramdisk] flag")
                Usage()
                exit()
        elif opt_name == '-r':
            ramdisk_path = opt_value
            if ((os.path.isdir(ramdisk_path) == False) and (using_ramdisk == 1)):
                print("invalid ramdisk path")
                Usage()
                exit()
        elif opt_name == '-h':
            Usage()
            exit()
        else:
            Usage()
            exit()

    trace_list = GetAllTrace(input_folder)
    output_script_file = open(output_script_name, 'w')

    # 数据集名 = -i 路径最后一级目录，用于拼接 -o 参数
    dataset_name = os.path.basename(os.path.normpath(input_folder))

    # 计算 -o 的值（如果 -d 已指定）
    cmd_output_dir = ""
    if download_root != "":
        cmd_output_dir =download_root
        # os.path.join(download_root, dataset_name)

    for idx, trace in enumerate(trace_list):
        current_cid = GetClientId(idx, batch_size, start_client_id, fixed_client_id)

        if (using_ramdisk == 0):
            trace_full_path = os.path.join(input_folder, trace)
            base_cmd = (program + " -t " + operation_type
                + " -i " + trace_full_path + " -m " + method_type
                + " -c " + current_cid)
            # 上传和下载都带上 -o（如果指定了 -d）
            if cmd_output_dir != "":
                base_cmd = base_cmd + " -o " + cmd_output_dir
            output_script_file.write(base_cmd + "\n")

            # 下载模式下清理恢复文件
            if (operation_type == "d" and restore_file_stat == 0):
                if cmd_output_dir != "":
                    restore_path = os.path.join(cmd_output_dir, trace)
                else:
                    restore_path = trace_full_path
                cmd = "rm " + restore_path + "-d-" + current_cid
                output_script_file.write(cmd + "\n")
        else:
            trace_full_path = os.path.join(input_folder, trace)
            if (operation_type == "u"):
                cmd = "cp " + trace_full_path + " " + ramdisk_path
                output_script_file.write(cmd + "\n")
                trace_ramdisk_path = os.path.join(ramdisk_path, trace)
                cmd = (program + " -t " + operation_type
                    + " -i " + trace_ramdisk_path + " -m " + method_type
                    + " -c " + current_cid)
                if cmd_output_dir != "":
                    cmd = cmd + " -o " + cmd_output_dir
                output_script_file.write(cmd + "\n")
                cmd = "rm " + trace_ramdisk_path
                output_script_file.write(cmd + "\n")
            else:
                trace_ramdisk_path = os.path.join(ramdisk_path, trace)
                cmd = (program + " -t " + operation_type
                    + " -i " + trace_ramdisk_path + " -m " + method_type
                    + " -c " + current_cid)
                if cmd_output_dir != "":
                    cmd = cmd + " -o " + cmd_output_dir
                output_script_file.write(cmd + "\n")
                # 清理 ramdisk 中的恢复文件
                if cmd_output_dir != "":
                    restore_path = os.path.join(cmd_output_dir, trace)
                else:
                    restore_path = trace_ramdisk_path
                cmd = "rm " + restore_path + "-d-" + current_cid
                output_script_file.write(cmd + "\n")
    output_script_file.close()
