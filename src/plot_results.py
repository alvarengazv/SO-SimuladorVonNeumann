import pandas as pd
import matplotlib.pyplot as plt
import os
import sys

# Caminhos
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
OUTPUT_DIR = os.path.abspath(os.path.join(SCRIPT_DIR, "..", "output"))

def ensure_baseline_exists():
    """Recria dados_baseline.csv se estiver faltando, usando final_speedup.txt"""
    csv_file = os.path.join(OUTPUT_DIR, "dados_baseline.csv")
    txt_file = os.path.join(OUTPUT_DIR, "final_speedup.txt")
    
    if not os.path.exists(csv_file):
        print(f"[AVISO] '{csv_file}' não encontrado. Tentando recuperar...")
        if os.path.exists(txt_file):
            try:
                # Lê o txt formato: "Baseline: 12868\nOtimizado: 3217..."
                data = {}
                with open(txt_file, 'r') as f:
                    for line in f:
                        if ":" in line:
                            key, val = line.split(":")
                            if "Speedup" not in key:
                                data[key.strip()] = float(val.strip().replace('x',''))
                
                if "Baseline" in data and "Otimizado" in data:
                    with open(csv_file, 'w') as f:
                        f.write("Config,Makespan\n")
                        f.write(f"Baseline,{data['Baseline']}\n")
                        f.write(f"Otimizado,{data['Otimizado']}\n")
                    print(f"[SUCESSO] Arquivo recuperado via final_speedup.txt!")
                    return True
            except: pass
        print("[FALHA] Não foi possível recuperar os dados de Baseline.")
        return False
    return True

def plot_all():
    print(f"Lendo arquivos de: {OUTPUT_DIR}")
    
    # Verifica e recupera arquivo se necessário
    ensure_baseline_exists()

    # -------------------------------------------------------
    # GRÁFICO 1: BASELINE VS OTIMIZADO (SPEEDUP)
    # -------------------------------------------------------
    try:
        file = os.path.join(OUTPUT_DIR, "dados_baseline.csv")
        if os.path.exists(file):
            df = pd.read_csv(file)
            plt.figure(figsize=(7, 5))
            bars = plt.bar(df["Config"], df["Makespan"], color=['gray', 'green'], width=0.6)
            plt.title('Impacto da Otimização (Multicore + Round Robin)')
            plt.ylabel('Tempo de Execução (Ciclos)')
            plt.grid(axis='y', alpha=0.3)
            
            for bar in bars:
                height = bar.get_height()
                plt.text(bar.get_x() + bar.get_width()/2.0, height, f'{int(height)}', ha='center', va='bottom')

            plt.savefig(os.path.join(OUTPUT_DIR, "grafico_baseline_final.png"), dpi=300)
            print(f"[OK] Gerado: grafico_baseline_final.png")
            plt.close()
        else:
            print("[PULAR] Baseline (Dados ausentes)")
    except Exception as e: print(f"[ERRO] Baseline: {e}")

    # -------------------------------------------------------
    # GRÁFICO 2: PERFORMANCE DOS ESCALONADORES
    # -------------------------------------------------------
    try:
        file = os.path.join(OUTPUT_DIR, "fatorial_workload.csv")
        if os.path.exists(file):
            df = pd.read_csv(file)
            df_avg = df.groupby("Scheduler")["Makespan"].mean().reset_index().sort_values("Makespan")
            
            plt.figure(figsize=(8, 5))
            plt.bar(df_avg["Scheduler"], df_avg["Makespan"], color='skyblue', edgecolor='black')
            plt.title('Desempenho Médio por Algoritmo de Escalonamento')
            plt.ylabel('Tempo Médio (Ciclos)')
            plt.grid(axis='y', alpha=0.3)
            plt.savefig(os.path.join(OUTPUT_DIR, "grafico_schedulers.png"), dpi=300)
            print(f"[OK] Gerado: grafico_schedulers.png")
            plt.close()
    except Exception as e: print(f"[ERRO] Schedulers: {e}")

    # -------------------------------------------------------
    # GRÁFICO 3: TAMANHO DA PÁGINA
    # -------------------------------------------------------
    try:
        file = os.path.join(OUTPUT_DIR, "fatorial_page_size.csv")
        if os.path.exists(file):
            df = pd.read_csv(file)
            # Tenta filtrar RR/FIFO, se falhar pega média
            df_filtered = df[(df["Scheduler"] == "Round Robin") & (df["CachePol"] == "FIFO")]
            if df_filtered.empty: df_filtered = df.groupby("Page Size")["Total Mem Cycles"].mean().reset_index()

            plt.figure(figsize=(7, 5))
            plt.plot(df_filtered["Page Size"], df_filtered["Total Mem Cycles"], marker='o', color='orange')
            plt.title('Impacto do Tamanho da Página')
            plt.xlabel('Tamanho da Página (Bytes)')
            plt.ylabel('Ciclos Totais de Memória')
            plt.grid(True, alpha=0.3)
            plt.savefig(os.path.join(OUTPUT_DIR, "grafico_page_size.png"), dpi=300)
            print(f"[OK] Gerado: grafico_page_size.png")
            plt.close()
    except Exception as e: print(f"[ERRO] Page Size: {e}")

    # -------------------------------------------------------
    # GRÁFICO 4: USO DA MEMÓRIA AO LONGO DO TEMPO
    # -------------------------------------------------------
    # Adicionar o tamanho da memória primaria, secundária e cache no gráfico
    # além do tamanho do pageSize, lineSize, blockSize e politicas de substituição da cache e memoria primaria
    # e o tipo de escalonador utilizado em uma legenda no gráfico

    try:
        # nome com data e hora para evitar sobrescrever
        from datetime import datetime
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")

        file = os.path.join(OUTPUT_DIR, f"memory_usage.csv")
        if os.path.exists(file):
            df = pd.read_csv(file)
            plt.figure(figsize=(10, 6))
            plt.plot(df["Timestamp"], df["CacheUsage(%)"], label='Cache', color='blue')
            plt.plot(df["Timestamp"], df["RAMUsage(%)"], label='RAM', color='green')
            plt.plot(df["Timestamp"], df["DiskUsage(%)"], label='Disco', color='red')
            plt.title('Uso da Memória ao Longo do Tempo')
            plt.xlabel('Tempo (Ciclos)')
            plt.ylabel('Uso da Memória (%)')
            # PrimaryMemorySize,PrimaryMemoryPageSize,PrimaryMemoryPolicy,SecondaryMemorySize,SecondaryMemoryBlockSize,SecondaryMemoryPolicy,CacheSize,CacheLineSize,CachePolicy,NumCores,Scheduler
            # pegar os valores da primeira linha para colocar na legenda
            plt.legend(title=f"Tamanho da Memória Primária: {df["PrimaryMemorySize"].iloc[0]} Bytes\nTamanho da Memória Secundária: {df["SecondaryMemorySize"].iloc[0]} Bytes\nCache: {df["CacheSize"].iloc[0]} Bytes\nTamanho da Página: {df["PrimaryMemoryPageSize"].iloc[0]} Bytes\nTamanho da Linha: {df["CacheLineSize"].iloc[0]} Bytes\nPolítica Cache: {df["CachePolicy"].iloc[0]}\nPolítica Memória Primária: {df["PrimaryMemoryPolicy"].iloc[0]}\nEscalonador: {df["Scheduler"].iloc[0]}")
            plt.grid(True, alpha=0.3)
            plt.savefig(os.path.join(OUTPUT_DIR, f"grafico_memory_usage_{timestamp}.png"), dpi=300)
            print(f"[OK] Gerado: grafico_memory_usage_{timestamp}.png")
            plt.close()
    except Exception as e: print(f"[ERRO] Memory Usage: {e}")

if __name__ == "__main__":
    plot_all()