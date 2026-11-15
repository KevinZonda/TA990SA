# TA990SA DriverOS Modification

We sperate this instruction into several modules. A general module hierarchy is like:

```
$module/
  -> build/ <-- exported binary
  -> README.md <-- Instruction
```

```
/var/lib/
  -> fake_usr <- Fake usr
    -> bin    <- binaries
    -> lib    <- libraries
        -> $libName e.g., mergerfs
```

## FileSystem Module (`fs`)

We use `mergerfs` to merge fs into one "path".

```sh
root@tegra-ubuntu:/var/lib/fake_usr/bin# ll
total 8
drwxr-xr-x. 2 root root 4096 Feb 27 03:44 ./
drwxr-xr-x. 4 root root 4096 Feb 27 03:42 ../
lrwxrwxrwx. 1 root root   24 Feb 27 03:44 mergerfs -> ../lib/mergerfs/mergerfs*
lrwxrwxrwx. 1 root root   35 Feb 27 03:44 mergerfs-fusermount -> ../lib/mergerfs/mergerfs-fusermount*
lrwxrwxrwx. 1 root root   30 Feb 27 03:44 mount.mergerfs -> ../lib/mergerfs/mount.mergerfs*
```


```sh
# Load Kernel Module
modprobe fuse

# Create fusion pathes
mkdir -p /opt/m/fusion_data \
         /opt/m0/fusion_data \
         /opt/update/fusion_data
mkdir -p /var/lib/fusion_data

# Temporary Mount
/var/lib/fake_usr/bin/mergerfs \
  -o category.create=mfs,minfreespace=2G,cache.files=partial,dropcacheonclose=true,allow_other \
  /opt/m/fusion_data:/opt/m0/fusion_data:/opt/update/fusion_data \
  /var/lib/fusion_data

df -h /var/lib/fusion_data

# Unmount
# umount /var/lib/fusion_data
```

Save to `
/etc/systemd/system/mergerfs-data.service`

```
[Unit]
Description=mergerfs data pool
RequiresMountsFor=/opt/m /opt/m0 /opt/update /var/lib/fusion_data

[Service]
Type=simple
ExecStartPre=/usr/sbin/modprobe fuse
ExecStart=/var/lib/fake_usr/bin/mergerfs -f \
    -o category.create=mfs,minfreespace=2G,cache.files=partial,dropcacheonclose=true,allow_other \
    /opt/m/fusion_data:/opt/m0/fusion_data:/opt/update/fusion_data \
    /var/lib/fusion_data
ExecStop=/bin/umount /var/lib/fusion_data
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
```

## CUDA (`cuda`)

`cuda-probe` -> `/var/lib/fake_usr/lib`

```
CUDA_CACHE_DISABLE=1 timeout 60 /var/lib/fake_usr/lib/cuda-probe --all-tests
```

### Reboot without CUDA Panic

Generally, the Linux is under a hypervisor of driver os to be safe. The standard `reboot now` command may leads to GPU ACR init failure, e.g.,

```
HS ucode boot failed, err 1b
ACR register access failure
ACR bootstrap failed
Failed initialization for: g->ops.acr.acr_construct_execute
```

A better way to fix (or reboot the system) is via hypervisor reboot:

```sh
sync
echo 1 > /sys/class/tegra_hv_pm_ctl/tegra_hv_pm_ctl/device/trigger_sys_reboot
```
