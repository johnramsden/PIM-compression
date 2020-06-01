import os
import sys
import argparse
import pathlib
import numpy as np
import matplotlib.pyplot as plt
from parse_output_file import get_avg_max_cycles


def setup_graph(cfg: dict):
    """
    Setup and show the Graph
    :param cfg: Config data
    """
    idx = 0
    cycles = []
    for tasks in cfg.get("num_tasks"):
        # 2D list of size num_tasks by num_dpus
        cycles.append([0] * len(cfg.get("num_dpus")))

        dpu_idx = 0
        for dpus in cfg.get("num_dpus"):
            cycles[idx][dpu_idx] = get_avg_max_cycles(
                cfg.get("path"), cfg.get("testfile"), dpus, tasks) / 1000000
            dpu_idx += 1
        idx += 1

    # Print for easy debugging
    print(cycles)

    # Set up plot
    plt.rc('font', size=12)
    plt.rc('axes', titlesize=12)
    plt.rc('axes', labelsize=12)
    fig, ax = plt.subplots()

    # Set up positions of each bar
    ypos = []
    width = 0.13
    ypos.append(np.arange(len(cfg.get("num_dpus"))))
    for i in range(len(cfg.get("num_tasks")) - 1):
        ypos.append([x + width for x in ypos[i]])

    # Set up axes labels
    plt.xticks([r + width for r in range(len(cfg.get("num_dpus")))], cfg.get("num_dpus"))
    ax.set_xlabel('Number of DPUs')
    ax.set_ylabel('Cycle Count (in Millions)')

    for i in range(0, len(cfg.get("num_tasks"))):
        ax.bar(ypos[i], cycles[i], color=cfg.get("colors")[i], width=width, edgecolor='white')

    ax.legend([x + ' Tasklets' for x in cfg.get("num_tasks")])

    plt.show()


if __name__ == "__main__":
    color_default = '#c9b395'
    try:
        import config
    except ImportError as err:
        print("Config file 'config.py' does not exist", file=sys.stderr)
        exit(1)

    # Get the output file directory path
    parser = argparse.ArgumentParser(description='Create graph of cycle count by DPU and tasklets')
    requiredArgs = parser.add_argument_group('required arguments')
    requiredArgs.add_argument('PATH', help='directory holding output files to parse')
    requiredArgs.add_argument('FILENAME', help='test file name without file ending')

    args = parser.parse_args()
    path = pathlib.Path(args.PATH)
    testfile = args.FILENAME
    if not path.is_dir():
        raise argparse.ArgumentTypeError(f"{path} is not a valid path")

    conf = {
        "num_tasks": config.num_tasks,
        "num_dpus": config.num_dpus,
        "colors": config.colors,
        "testfile": testfile,
        "path": path
    }

    color_len = len(conf.get("colors"))
    task_len = len(conf.get("num_tasks"))
    if len(conf.get("num_tasks")) > color_len:
        print("WARNING: num_tasks is larger than colors.\n"
              f"{color_default} will be used for remaining colors.", file=sys.stderr)
        conf["colors"] = [
            conf["colors"][i] if i < color_len else color_default for i in range(task_len)
        ]

    setup_graph(conf)
