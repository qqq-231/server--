# server--
实验室服务器
方法：odess,odess_start,n_transform,n_transform_gear,finesse

--------------
命令脚本
## 这些是脚本的**默认值**，当你不传对应参数时生效：

|变量|默认值|含义|
|---|---|---|
|`input_folder`|`/mnt/hgfs/linux_2`|`-i`：扫描这个文件夹下所有文件，逐一生成命令|
|`output_script_name`|`run_linux_2.sh`|`-o`：生成的脚本文件名|
|`operation_type`|`u`|`-t`：`u`=上传，`d`=下载|
|`method_type`|`2`|`-m`：`0`=仅加密，`1`=加密+压缩，`2`=完整EDR|
|`fixed_client_id`|`1`|`-c`：client ID（`-a` 未启用时固定不变）|
|`batch_size`|`0`|`-a`：每 N 条后 client ID +1，`0`=不轮转|
|`start_client_id`|`1`|`-b`：`-a` 启用时，起始 client ID|
|`download_root`|空|`-d`：下载根目录，非空时生成 `-o 根目录/数据集名`|
|`restore_file_stat`|`0`|`-s`：下载后是否保留文件，`0`=删除，`1`=保留|
|`using_ramdisk`|`0`|`-f`：是否用内存盘，`0`=否，`1`=先 cp 到 ramdisk 再操作|
|`ramdisk_path`|空|`-r`：内存盘路径（`-f 1` 时必填）|
---------------

