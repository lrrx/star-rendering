import pandas as pd
import seaborn as sns
import matplotlib.pyplot as plt

# 1. Load and pivot the data (as done in Option 1)
df = pd.read_csv('benchmark_results.csv')
pivot_table = df.pivot(index='ParamB', columns='ParamA', values='Frametime')

# 2. Set up the visual style
plt.figure(figsize=(10, 8))

# 3. Create the heatmap
# annot=True: This writes the data value in each cell
# fmt=".3f": This formats the numbers to 3 decimal places
# cmap="ِّYlGnBu": This sets the color palette (Yellow-Green-Blue)
# cbar_kws: Allows us to label the color bar
sns.heatmap(pivot_table, 
            annot=True, 
            fmt=".3f", 
            cmap="YlGnBu", 
            linewidths=.5, 
            cbar_kws={'label': 'Frametime'})

# 4. Add labels and title
plt.title('Benchmark Performance Heatmap', fontsize=16)
plt.xlabel('Param A', fontsize=12)
plt.ylabel('Param B', fontsize=12)

# Display the plot
plt.show()