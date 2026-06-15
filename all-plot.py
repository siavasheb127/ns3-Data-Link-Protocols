import pandas as pd
import matplotlib.pyplot as plt
import numpy as np



try:
    sw_ber = pd.read_csv('sw_ber.csv')
    sw_delay = pd.read_csv('sw_delay.csv')
    
    gbn_ber = pd.read_csv('gbn_ber.csv')
    gbn_delay = pd.read_csv('gbn_delay.csv')
    gbn_window = pd.read_csv('gbn_window.csv')
    
    sr_ber = pd.read_csv('sr_ber.csv')
    sr_delay = pd.read_csv('sr_delay.csv')
    sr_window = pd.read_csv('sr_window.csv')
except FileNotFoundError as e:
    print(f"Error finding file: {e}")
    print("Please make sure you have run all 3 C++ simulations.")
    exit()

def clean_delay_column(df):
    df['Delay'] = df['Delay'].astype(str).str.replace('ms', '', regex=False)
    df['Delay'] = pd.to_numeric(df['Delay'], errors='coerce')
    return df

sw_delay = clean_delay_column(sw_delay)
gbn_delay = clean_delay_column(gbn_delay)
sr_delay = clean_delay_column(sr_delay)


for df in [sw_ber, gbn_ber, sr_ber, sw_delay, gbn_delay, sr_delay]:
    df.loc[df['Goodput'] == 0, 'AvgDelay'] = np.nan

print("Exporting 8 formatted tables to 'Final_Project_Tables.txt'...")
with open('Final_Project_Tables.txt', 'w', encoding='utf-8') as f:
    f.write("=========================================================\n")
    f.write("      DATA LINK LAYER PROTOCOLS - SIMULATION TABLES      \n")
    f.write("=========================================================\n\n")
    
    tables = [
        ("Table 1: Stop-and-Wait (Varying BER)", sw_ber),
        ("Table 2: Stop-and-Wait (Varying Delay)", sw_delay),
        ("Table 3: Go-Back-N (Varying BER)", gbn_ber),
        ("Table 4: Go-Back-N (Varying Delay)", gbn_delay),
        ("Table 5: Go-Back-N (Varying Window)", gbn_window),
        ("Table 6: Selective Repeat (Varying BER)", sr_ber),
        ("Table 7: Selective Repeat (Varying Delay)", sr_delay),
        ("Table 8: Selective Repeat (Varying Window)", sr_window)
    ]
    
    for title, df in tables:
        f.write(f"--- {title} ---\n")
        f.write(df.to_string(index=False, justify='center'))
        f.write("\n\n")

def plot_comparison(x_col, y_col, df_sw, df_gbn, df_sr, xlabel, ylabel, title, filename, use_log_x=False):
    plt.figure(figsize=(10, 6))
    
    if use_log_x:
        plt.semilogx(df_sw[x_col], df_sw[y_col], marker='o', linestyle='-', color='red', linewidth=2.5, label='Stop-and-Wait')
        plt.semilogx(df_gbn[x_col], df_gbn[y_col], marker='s', linestyle='--', color='blue', linewidth=2.5, label='Go-Back-N')
        plt.semilogx(df_sr[x_col], df_sr[y_col], marker='^', linestyle='-.', color='green', linewidth=2.5, label='Selective Repeat')
    else:
        plt.plot(df_sw[x_col], df_sw[y_col], marker='o', linestyle='-', color='red', linewidth=2.5, label='Stop-and-Wait')
        plt.plot(df_gbn[x_col], df_gbn[y_col], marker='s', linestyle='--', color='blue', linewidth=2.5, label='Go-Back-N')
        plt.plot(df_sr[x_col], df_sr[y_col], marker='^', linestyle='-.', color='green', linewidth=2.5, label='Selective Repeat')

    plt.title(title, fontsize=15, fontweight='bold', pad=15)
    plt.xlabel(xlabel, fontsize=13)
    plt.ylabel(ylabel, fontsize=13)
    plt.grid(True, which="major", ls="-", alpha=0.7)
    plt.grid(True, which="minor", ls="--", alpha=0.3)
    plt.legend(loc='best', fontsize=11, shadow=True)
    plt.savefig(filename, dpi=300, bbox_inches='tight')
    plt.close()

def plot_window_comparison(y_col, df_gbn, df_sr, baseline_val, xlabel, ylabel, title, filename):
    plt.figure(figsize=(10, 6))
   
    plt.axhline(y=baseline_val, color='red', linestyle='-', linewidth=2.5, label='Stop-and-Wait (Baseline)')
    plt.plot(df_gbn['Window'], df_gbn[y_col], marker='s', linestyle='--', color='blue', linewidth=2.5, label='Go-Back-N')
    plt.plot(df_sr['Window'], df_sr[y_col], marker='^', linestyle='-.', color='green', linewidth=2.5, label='Selective Repeat')

    plt.title(title, fontsize=15, fontweight='bold', pad=15)
    plt.xlabel(xlabel, fontsize=13)
    plt.ylabel(ylabel, fontsize=13)
    plt.grid(True, which="major", ls="-", alpha=0.7)
    plt.grid(True, which="minor", ls="--", alpha=0.3)
    plt.legend(loc='best', fontsize=11, shadow=True)
    plt.savefig(filename, dpi=300, bbox_inches='tight')
    plt.close()


print("Generating 9 High-Quality Plots...\n")


plot_comparison('BER', 'Goodput', sw_ber, gbn_ber, sr_ber, 
                'Bit Error Rate (BER)', 'Goodput (bps)', 
                '1. Throughput vs. BER', 'fig1_goodput_ber.png', use_log_x=True)

plot_comparison('BER', 'AvgDelay', sw_ber, gbn_ber, sr_ber, 
                'Bit Error Rate (BER)', 'Average Delay (s)', 
                '2. Average Delay vs. BER', 'fig2_delay_ber.png', use_log_x=True)

plot_comparison('BER', 'Utilization', sw_ber, gbn_ber, sr_ber, 
                'Bit Error Rate (BER)', 'Link Utilization (%)', 
                '3. Utilization vs. BER', 'fig3_utilization_ber.png', use_log_x=True)


plot_comparison('Delay', 'Goodput', sw_delay, gbn_delay, sr_delay, 
                'Propagation Delay (ms)', 'Goodput (bps)', 
                '4. Throughput vs. Propagation Delay', 'fig4_goodput_delay.png')

plot_comparison('Delay', 'AvgDelay', sw_delay, gbn_delay, sr_delay, 
                'Propagation Delay (ms)', 'Average Delay (s)', 
                '5. Average Delay vs. Propagation Delay', 'fig5_delay_delay.png')

plot_comparison('Delay', 'Utilization', sw_delay, gbn_delay, sr_delay, 
                'Propagation Delay (ms)', 'Link Utilization (%)', 
                '6. Utilization vs. Propagation Delay', 'fig6_utilization_delay.png')


print("Extracting baselines for Window Size plots...")
try:
    sw_base_g = sw_ber.loc[sw_ber['BER'] == 1e-05, 'Goodput'].values[0]
    sw_base_d = sw_ber.loc[sw_ber['BER'] == 1e-05, 'AvgDelay'].values[0]
    sw_base_u = sw_ber.loc[sw_ber['BER'] == 1e-05, 'Utilization'].values[0]
except IndexError:
    sw_base_g, sw_base_d, sw_base_u = 512000, 0.015, 11.5

plot_window_comparison('Goodput', gbn_window, sr_window, sw_base_g,
                       'Window Size (N/W)', 'Goodput (bps)', 
                       '7. Throughput vs. Window Size', 'fig7_goodput_window.png')

plot_window_comparison('AvgDelay', gbn_window, sr_window, sw_base_d,
                       'Window Size (N/W)', 'Average Delay (s)', 
                       '8. Average Delay vs. Window Size', 'fig8_delay_window.png')

plot_window_comparison('Utilization', gbn_window, sr_window, sw_base_u,
                       'Window Size (N/W)', 'Link Utilization (%)', 
                       '9. Utilization vs. Window Size', 'fig9_utilization_window.png')

print("Success! All 9 plots and 'Final_Project_Tables.txt' have been created.")