import json
import os
import subprocess
import shutil
import re
import csv
import matplotlib.pyplot as plt
import copy
import platform

# --- CONFIGURAÇÕES GERAIS ---
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))
PATH_CONFIG = os.path.join(PROJECT_ROOT, "src", "system_config", "system_config.json")

# Tasks e Templates
PATH_TASKS_1 = os.path.join(PROJECT_ROOT, "src", "tasks", "tasks.json")
PATH_TASKS_2 = os.path.join(PROJECT_ROOT, "src", "tasks", "tasks_counter.json")
PATH_TASKS_3 = os.path.join(PROJECT_ROOT, "src", "tasks", "tasks_forward.json")
PATH_TASKS_4 = os.path.join(PROJECT_ROOT, "src", "tasks", "tasks_io.json")
ALL_TASK_PATHS = [PATH_TASKS_1, PATH_TASKS_2, PATH_TASKS_3, PATH_TASKS_4]

PATH_TEMPLATE_CPU = os.path.join(PROJECT_ROOT, "src", "tasks", "tasks_counter.json")
PATH_TEMPLATE_IO = os.path.join(PROJECT_ROOT, "src", "tasks", "tasks_io.json")

# Backups
PATH_CONFIG_BAK = PATH_CONFIG + ".bak"
PATH_TASKS_BAKS = [p + ".bak" for p in ALL_TASK_PATHS]

# Executável
SYSTEM_OS = platform.system()
EXE_NAME = "simulador.exe" if SYSTEM_OS == "Windows" else "simulador"
POSSIBLE_PATHS = [
    os.path.join(PROJECT_ROOT, "src", EXE_NAME),
    os.path.join(PROJECT_ROOT, EXE_NAME),
    os.path.join(SCRIPT_DIR, EXE_NAME)
]
EXE_FULL_PATH = None
for p in POSSIBLE_PATHS:
    if os.path.exists(p):
        EXE_FULL_PATH = p
        break
if not EXE_FULL_PATH:
    EXE_FULL_PATH = os.path.join(PROJECT_ROOT, "src", EXE_NAME)

PATH_LOG = os.path.join(PROJECT_ROOT, "output", "resultados.dat")
PATH_OUTPUT_DIR = os.path.join(PROJECT_ROOT, "output")

# --- MAPAS DE LEGENDAS (Para o CSV ficar legível) ---
SCHEDULER_NAMES = {
    0: "Round Robin",
    1: "FCFS",
    2: "SJF",
    3: "SRTF",
    4: "Priority"
}
POLICY_NAMES = {
    0: "FIFO",
    1: "LRU"
}

# --- FUNÇÕES AUXILIARES ---

def backup_files():
    if os.path.exists(PATH_CONFIG): shutil.copy(PATH_CONFIG, PATH_CONFIG_BAK)
    for i, path in enumerate(ALL_TASK_PATHS):
        if os.path.exists(path): shutil.copy(path, PATH_TASKS_BAKS[i])

def restore_files():
    if os.path.exists(PATH_CONFIG_BAK):
        shutil.copy(PATH_CONFIG_BAK, PATH_CONFIG)
        os.remove(PATH_CONFIG_BAK)
    for i, path in enumerate(ALL_TASK_PATHS):
        if os.path.exists(PATH_TASKS_BAKS[i]):
            shutil.copy(PATH_TASKS_BAKS[i], path)
            os.remove(PATH_TASKS_BAKS[i])

def load_json(path):
    if not os.path.exists(path): return {"program": [{"instruction": "end"}]}
    with open(path, 'r') as f: return json.load(f)

def save_json(path, data):
    with open(path, 'w') as f: json.dump(data, f, indent=4)

def append_to_csv(filename, header, rows):
    """Adiciona linhas ao CSV. Cria o arquivo com header se não existir."""
    filepath = os.path.join(PATH_OUTPUT_DIR, filename)
    file_exists = os.path.isfile(filepath)
    
    try:
        with open(filepath, 'a', newline='', encoding='utf-8') as f:
            w = csv.writer(f)
            if not file_exists:
                w.writerow(header) # Escreve header apenas na primeira vez
            w.writerows(rows)
    except Exception as e: print(f"Erro CSV: {e}")

# --- GERADOR DE CARGA ---

def create_dummy_task():
    return {
        "metadata": {"name": "dummy", "description": "idle"},
        "data": {},
        "program": [{"instruction": "end"}]
    }

def generate_workload(type_str, count):
    template_path = PATH_TEMPLATE_CPU if type_str == 'cpu' else PATH_TEMPLATE_IO
    task_template = load_json(template_path)
    count = min(count, 4)
    for i in range(4):
        target_path = ALL_TASK_PATHS[i]
        if i < count:
            new_task = copy.deepcopy(task_template)
            if "metadata" in new_task:
                new_task["metadata"]["name"] = f"{type_str}_copy_{i+1}"
            save_json(target_path, new_task)
        else:
            save_json(target_path, create_dummy_task())

# --- EXECUÇÃO E PARSER ---

def compile_project():
    print(f"--- Compilando ({SYSTEM_OS}) ---")
    try:
        make_target = "src/simulador"
        subprocess.run(["make", make_target], cwd=PROJECT_ROOT, check=True, shell=True, stdout=subprocess.DEVNULL)
        global EXE_FULL_PATH
        for p in POSSIBLE_PATHS:
            if os.path.exists(p):
                EXE_FULL_PATH = p
                break
    except:
        try:
            subprocess.run(["make"], cwd=PROJECT_ROOT, check=True, shell=True, stdout=subprocess.DEVNULL)
        except: pass

def run_simulation():
    try:
        subprocess.run([EXE_FULL_PATH], cwd=PROJECT_ROOT, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=5)
    except subprocess.TimeoutExpired:
        print("  [AVISO] Timeout.")
    except Exception: pass

def parse_results(cores=1):
    if not os.path.exists(PATH_LOG): return {"makespan": 0, "total_mem": 0, "misses": 0}
    stats_by_pid = {}
    with open(PATH_LOG, 'r', encoding='utf-8', errors='ignore') as f: content = f.read()
    
    blocks = content.split("------------------------------------------")
    for block in blocks:
        pid_match = re.search(r'\[Processo PID (\d+)\]|METRICAS FINAIS DO PROCESSO (\d+)', block)
        if not pid_match: continue
        pid = int(pid_match.group(1) or pid_match.group(2))
        
        metrics = {}
        et = re.search(r'Tempo Total de Execução:\s*(\d+)', block)
        if et: metrics['exec_time'] = int(et.group(1))
        
        mem = re.search(r'Ciclos Totais de Memoria:\s*(\d+)', block)
        if mem: metrics['mem_cycles'] = int(mem.group(1))
        
        miss = re.search(r'- Misses:\s*(\d+)', block)
        if miss: metrics['cache_misses'] = int(miss.group(1))
        
        if pid not in stats_by_pid: stats_by_pid[pid] = {}
        stats_by_pid[pid].update(metrics)
        
    sys_stats = {"makespan": 0, "total_mem": 0, "misses": 0}
    total_workload = 0
    
    # Filtra processos dummy (tempo muito baixo)
    for pid, d in stats_by_pid.items():
        if d.get('exec_time', 0) > 5:
            total_workload += d.get('exec_time', 0)
            sys_stats['total_mem'] += d.get('mem_cycles', 0)
            sys_stats['misses'] += d.get('cache_misses', 0)
            
    sys_stats['makespan'] = total_workload / cores if cores > 0 else 0
    return sys_stats

# --- EXPERIMENTOS MODULARES (Aceitam config externa) ---

def exp_page_size(current_config, context_info):
    # context_info: [SchedName, CachePol, RamPol]
    base = copy.deepcopy(current_config)
    sizes = [64, 128, 256, 512]
    rows = []
    
    for s in sizes:
        base["main_memory"]["page_size"] = s
        save_json(PATH_CONFIG, base)
        generate_workload('cpu', 1) 
        run_simulation()
        st = parse_results(cores=base["cpu"]["cores"])
        
        # Adiciona o contexto nas colunas iniciais
        row = context_info + [s, st['total_mem'], st['misses']]
        rows.append(row)
        
    append_to_csv("fatorial_page_size.csv", ["Scheduler", "CachePol", "RamPol", "Page Size", "Total Mem Cycles", "Cache Misses"], rows)

def exp_cpu_vs_io(current_config, context_info):
    base = copy.deepcopy(current_config)
    # Fixa 2 cores para este teste
    base["cpu"]["cores"] = 2
    save_json(PATH_CONFIG, base)
    
    rows = []
    
    # CPU Bound
    generate_workload('cpu', 1) 
    run_simulation()
    st = parse_results(cores=2)
    rows.append(context_info + ["CPU-Bound", st['makespan']])
    
    # IO Bound
    generate_workload('io', 1)
    run_simulation()
    st = parse_results(cores=2)
    rows.append(context_info + ["IO-Bound", st['makespan']])
    
    append_to_csv("fatorial_workload.csv", ["Scheduler", "CachePol", "RamPol", "Workload Type", "Makespan"], rows)

def exp_multiprogramming(current_config, context_info):
    base = copy.deepcopy(current_config)
    base["cpu"]["cores"] = 2
    save_json(PATH_CONFIG, base)
    
    counts = [1, 2, 4]
    rows = []
    
    for c in counts:
        generate_workload('cpu', c)
        run_simulation()
        st = parse_results(cores=2)
        rows.append(context_info + [c, st['makespan']])
        
    append_to_csv("fatorial_scalability.csv", ["Scheduler", "CachePol", "RamPol", "Process Count", "Makespan"], rows)

# --- EXPERIMENTO ESPECIAL (FORA DO LOOP) ---
def exp_baseline_vs_optimal():
    print("\n[Especial] Baseline vs Otimizado (Speedup Final)")
    base = load_json(PATH_CONFIG)
    
    # Baseline
    base["cpu"]["cores"] = 1
    base["scheduling"]["algorithm"] = 1 # FCFS
    base["cache"]["size"] = 16
    base["main_memory"]["total"] = 1024
    save_json(PATH_CONFIG, base)
    generate_workload('cpu', 4)
    run_simulation()
    t_base = parse_results(cores=1)['makespan']
    
    # Otimizado
    base["cpu"]["cores"] = 4
    base["scheduling"]["algorithm"] = 0 # RR
    base["cache"]["size"] = 256
    base["main_memory"]["total"] = 8192
    save_json(PATH_CONFIG, base)
    run_simulation()
    t_opt = parse_results(cores=4)['makespan']
    
    speedup = (t_base / t_opt) if t_opt > 0 else 0
    print(f"  => SPEEDUP ALCANÇADO: {speedup:.2f}x")
    
    # Salva isolado
    with open(os.path.join(PATH_OUTPUT_DIR, "final_speedup.txt"), "w") as f:
        f.write(f"Baseline: {t_base}\nOtimizado: {t_opt}\nSpeedup: {speedup:.2f}x")

# --- MASTER LOOP ---

def run_full_factorial():
    # Remove CSVs antigos para não misturar dados
    for f in ["fatorial_page_size.csv", "fatorial_workload.csv", "fatorial_scalability.csv"]:
        full_p = os.path.join(PATH_OUTPUT_DIR, f)
        if os.path.exists(full_p): os.remove(full_p)

    base_config = load_json(PATH_CONFIG)
    
    # LISTAS DE VARIAÇÃO
    # Schedulers: 0=RR, 1=FCFS, 2=SJF, 3=SRTF, 4=Priority
    schedulers = [0, 1, 2, 3, 4] 
    # Policies: 0=FIFO, 1=LRU
    cache_policies = [0, 1] 
    ram_policies = [0, 1]
    
    total_iter = len(schedulers) * len(cache_policies) * len(ram_policies)
    curr_iter = 0
    
    print(f"--- INICIANDO EXPERIMENTO FATORIAL ({total_iter} combinações) ---")
    
    for sched in schedulers:
        for c_pol in cache_policies:
            for r_pol in ram_policies:
                curr_iter += 1
                s_name = SCHEDULER_NAMES.get(sched, str(sched))
                c_name = POLICY_NAMES.get(c_pol, str(c_pol))
                r_name = POLICY_NAMES.get(r_pol, str(r_pol))
                
                print(f"[{curr_iter}/{total_iter}] Config: {s_name} | Cache: {c_name} | RAM: {r_name}")
                
                # Configura o ambiente atual
                base_config["scheduling"]["algorithm"] = sched
                base_config["cache"]["policy"] = c_pol
                base_config["main_memory"]["policy"] = r_pol
                
                # Dados de contexto para o CSV
                context = [s_name, c_name, r_name]
                
                # Roda os experimentos "filhos" com essa configuração
                exp_page_size(base_config, context)
                exp_cpu_vs_io(base_config, context)
                exp_multiprogramming(base_config, context)

# --- MAIN ---
if __name__ == "__main__":
    if shutil.which("make"):
        compile_project()
    else:
        print("Aviso: 'make' não encontrado.")

    backup_files()
    if not os.path.exists(PATH_OUTPUT_DIR): os.makedirs(PATH_OUTPUT_DIR)

    try:
        # 1. Roda o loop gigante
        run_full_factorial()
        
        # 2. Roda o teste especial de Speedup no final
        exp_baseline_vs_optimal()
        
        print("\n=== TODOS OS DADOS GERADOS NA PASTA OUTPUT ===")
        print("Arquivos gerados:")
        print(" - fatorial_page_size.csv")
        print(" - fatorial_workload.csv")
        print(" - fatorial_scalability.csv")
        print(" - final_speedup.txt")
        
    except KeyboardInterrupt: print("\nParado.")
    except Exception as e: print(f"\nErro: {e}")
    finally: restore_files()