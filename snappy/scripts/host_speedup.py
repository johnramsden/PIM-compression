import argparse
import pathlib
import sys

import numpy as np
import matplotlib.pyplot as plt
from parse_output_file import get_avg_max_cycles, get_avg_host_runtime


def setup_graph(cfg: dict):
    """
    Setup and show the Graph
    :param cfg: Config data
    """
    # Loop through directory for respective output files and parse them
    cycles = []
    host_time = []
    for filename in cfg.get("files"):
        params = cfg.get("files")[filename]

        ahr = get_avg_host_runtime(cfg.get("path"), filename)
        amc = get_avg_max_cycles(cfg.get("path"), filename, params[0], params[1])

        if ahr is None:
            print(f"WARNING: No file {filename} was found for host, skipping.",
                  file=sys.stderr)

        if amc is None:
            print(f"WARNING: No file {filename} was found for cycles, skipping.",
                  file=sys.stderr)

        host_time.append(ahr)
        cycles.append(amc)

    # Calculate the speedup
    speedup = []
    for i in range(len(cfg.get("files"))):
        if cycles[i] is None or host_time[i] is None:
            continue

        dpu_time = float(cycles[i]) / 267000000  # DPU clock speed i 267MHz

        if host_time[i] < dpu_time:
            speedup.append((host_time[i] / dpu_time - 1) * 100)
        else:
            speedup.append((host_time[i] / dpu_time) * 100)

    # Print for easy debugging
    print(cfg.get("files"))
    print(speedup)

    # Set up plot
    plt.rc('font', size=12)
    plt.rc('axes', titlesize=12)
    plt.rc('axes', labelsize=12)
    fig, ax = plt.subplots()

    # y-axis labels
    yticks = np.arange(len(cfg.get("files")))
    ax.set_yticks(yticks)
    ax.set_yticklabels(cfg.get("files"))

    # x-axis labels
    xticks = np.arange(-100, 800, step=50)
    ax.set_xticks(xticks)
    ax.set_xlabel('Speedup Over Host Application (%)')
    ax.xaxis.grid(True, linestyle="dotted")

    if len(speedup) > 0:
        ax.barh(yticks, speedup,
                color=list(map(lambda x: '#d35e60' if (x < 0) else '#84ba5b', speedup)))

        plt.show()
    else:
        print(f"ERROR: No data found.", file=sys.stderr)
        exit(1)


if __name__ == "__main__":
    try:
        import config
    except ImportError as err:
        print("Config file 'config.py' does not exist", file=sys.stderr)
        exit(1)

    # Get the output file directory path
    parser = argparse.ArgumentParser(description='Create graph of DPU speedup over host')
    requiredArgs = parser.add_argument_group('required arguments')
    requiredArgs.add_argument('PATH', help='directory holding output files to parse')

    args = parser.parse_args()
    path = pathlib.Path(args.PATH)
    if not path.is_dir():
        raise argparse.ArgumentTypeError(f"{path} is not a valid path")

    conf = {
        "path": path,
        "files": config.files
    }

    setup_graph(conf)
