import os, time, subprocess, argparse, paramiko, psutil, socket

parser = argparse.ArgumentParser()
parser.add_argument('--cnt', type=int, help='Total execution counts', required=True)
parser.add_argument('--nic', type=str, help='Network interface name', required=True)
parser.add_argument('--des_ip', type=str, help='Destination IP Address', required=True)
parser.add_argument('--iperf_duration', type=int, help='Iperf duration', required=True)
parser.add_argument('--saturation_rate', type=str, help='Rate limit', required=True)
parser.add_argument('--tcpdump_capture_length', type=int, default=64)
args = parser.parse_args()

des_ip = args.des_ip
NIC = args.nic
cnt = args.cnt
iperf_duration = args.iperf_duration
saturation_rate = args.saturation_rate
tcpdump_capture_length = args.tcpdump_capture_length

# Customized
ssh_key_path = ""
ssh_user = ""
trace_host_path = ""
trace_server_path = ""

def check_folder(folder_path): 
    if not os.path.exists(folder_path):
        os.makedirs(folder_path)

def ssh_command(ssh, command):
    ssh.exec_command(command)

def create_ssh_connection(ip, user, key_path):
    ssh = paramiko.SSHClient()
    ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    try:
        ssh.connect(ip, username=user, key_filename=key_path)
    except paramiko.SSHException as e:
        print(f"SSH connection failed: {e}")
        return None
    return ssh

def get_ipv6_address(interface_name):
    addrs = psutil.net_if_addrs()
    if interface_name not in addrs:
        return None

    for addr in addrs[interface_name]:
        if addr.family == socket.AF_INET6:
            return addr.address.split('%')[0]
    return None

def main():
    for i in range(1, cnt+1):
        folder_path = ""
        output_file_name = ""
        host_ipv6 = get_ipv6_address(NIC) # IPV4 is also workable

        with create_ssh_connection(des_ip, ssh_user, ssh_key_path) as ssh:
            if ssh:
                ssh_command(ssh, 'iperf3 -s')
                ssh_command(ssh, f"mkdir -p {trace_server_path}/{folder_path}")
                ssh_command(ssh, f"sudo tcpdump -s {tcpdump_capture_length} -w {trace_server_path}/{folder_path}/{output_file_name}.pcap")
		
                ping_remote_output_path = f"{trace_server_path}/{folder_path}/{output_file_name}.txt"
                ssh_command(ssh, f"ping {host_ipv6} -i 0.005 -D > {ping_remote_output_path} 2>&1 &")
        
        check_folder(f'{trace_host_path}/{folder_path}')
        subprocess.Popen(
           f"tcpdump host {des_ip} -i {NIC} -s {tcpdump_capture_length} -w {trace_host_path}/{folder_path}/{output_file_name}.pcap",
           shell=True,
           stdout=subprocess.PIPE,
           stderr=subprocess.PIPE
        )

        subprocess.Popen([f"iperf3 -R -u -b {saturation_rate} -c {des_ip} -t {iperf_duration} &"], shell=True)
        time.sleep(iperf_duration)
        os.system("sudo pkill -f 'tcpdump|iperf'")

        with create_ssh_connection(des_ip, ssh_user, ssh_key_path) as ssh:
            if ssh:
                ssh_command(ssh, "sudo pkill -f 'ping|tcpdump|iperf'")

if __name__ == "__main__":
    main()
