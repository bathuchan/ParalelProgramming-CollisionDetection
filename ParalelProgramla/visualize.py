import matplotlib.pyplot as plt
import mplcursors as mplcursors
import os
import sys

def read_results_file():
    
    # Look for results.txt in current directory first
    file_path = "results.txt"
    if not os.path.exists(file_path):
        print(f"Error: {file_path} dosya bulunamadı!")
        sys.exit(1)
    
    try:
        with open(file_path, "r", encoding='utf-8') as f:
            lines = f.readlines()
    except Exception as e:
        print(f"Error reading file: {e}")
        sys.exit(1)
    
    if len(lines) < 3:
        print("Error: Hatalı dosya formatı ya da dosya boş!")
        sys.exit(1)
    
    # Parse polygon points
    polygon_points = []
    i = 1  # Skip first line (header)
    
    # Read polygon points until empty line
    while i < len(lines) and lines[i].strip() != "":
        try:
            parts = lines[i].strip().split()
            if len(parts) >= 2:
                x, y = float(parts[0]), float(parts[1])
                polygon_points.append([x, y])
        except ValueError as e:
            print(f"Error poligon noktasında hatalı girdi: {i+1}: {e}")
            sys.exit(1)
        i += 1
    
    if len(polygon_points) < 3:
        print("Error: En az 3 poligon noktası gereklidir!")
        sys.exit(1)

    polygon_points.append(polygon_points[0])
    
    # Find "Test Points:" line
    while i < len(lines) and "Test Points:" not in lines[i]:
        i += 1
    
    if i >= len(lines):
        print("Error: 'Test Points:'bölümü bulunamadı!")
        sys.exit(1)
    
    i += 1  
    
    # Parse test points and results
    test_points = []
    results = []
    
    for line_num in range(i, len(lines)):
        line = lines[line_num].strip()
        if not line:
            continue
            
        try:
            # Beklenen format: "x y => EVET" or "x y => HAYIR"
            if "=>" not in line:
                continue
                
            parts = line.split("=>")
            if len(parts) != 2:
                continue
                
            # Parse coordinates
            coords_part = parts[0].strip()
            coord_parts = coords_part.split()
            if len(coord_parts) < 2:
                continue
                
            x, y = float(coord_parts[0]), float(coord_parts[1])
            
            # Parse result
            status = parts[1].strip().upper()
            is_inside = 1 if status == "EVET" else 0
            
            test_points.append([x, y])
            results.append(is_inside)
            
        except (ValueError, IndexError) as e:
            print(f"Warning: Satır okumada hata: {line_num+1}: '{line}' - {e}")
            continue
    
    return polygon_points, test_points, results

def visualize_polygon_and_points(polygon_points, test_points, results):
    if not test_points:
        print("Warning: Test noktası girdisi bulunamadı!")
        return

    # içeride ve dışarıda kalan noktaları ayırma
    inside_points = [(x, y) for (x, y), r in zip(test_points, results) if r == 1]
    outside_points = [(x, y) for (x, y), r in zip(test_points, results) if r == 0]

    # poligon konum noktalarını ayırma
    px, py = zip(*polygon_points)

    # Yardımcı grafik ayarları
    fig, ax = plt.subplots(figsize=(10, 8))

    # Poligon bilgilerini çizme/yazdırma
    ax.plot(px, py, 'b-', linewidth=2, label=f"Poligon Kenarları")
    ax.fill(px, py, alpha=0.1, color='blue')

    # Poligon köşe noktalarını mavi dairelerle göster
    corner_x, corner_y = zip(*polygon_points[:-1])  # Son nokta ilk noktayla aynı, tekrar etmeyelim
    ax.scatter(corner_x, corner_y, c='blue', s=60, marker='o', edgecolors='black', label=f"Poligon Köşe Noktaları({len(polygon_points)-1} Köşeli)", zorder=6)

    # Test noktalarını çizme
    if inside_points:
        inside_x, inside_y = zip(*inside_points)
        inside_scatter = ax.scatter(inside_x, inside_y, c='green', marker='o', s=50, 
                                    label=f"İçeride ({len(inside_points)})", zorder=5)

    if outside_points:
        outside_x, outside_y = zip(*outside_points)
        outside_scatter = ax.scatter(outside_x, outside_y, c='red', marker='x', s=50, 
                                     label=f"Dışarıda ({len(outside_points)})", zorder=5)

    GREEN = '\033[92m'
    RED = '\033[91m'
    RESET = '\033[0m'

    print("\n--- Test Noktaları Sonuçları ---")
    for (x, y), r in zip(test_points, results):
        durum = "İÇERİDE" if r == 1 else "DIŞARIDA"
        color = GREEN if r == 1 else RED
        print(f"{color}({x:.2f}, {y:.2f}) => {durum}{RESET}")
    print("-------------------------------\n")
    # Kenar Boşlukları
    all_x = [p[0] for p in polygon_points + test_points]
    all_y = [p[1] for p in polygon_points + test_points]
    margin_x = (max(all_x) - min(all_x)) * 0.1
    margin_y = (max(all_y) - min(all_y)) * 0.1
    ax.set_xlim(min(all_x) - margin_x, max(all_x) + margin_x)
    ax.set_ylim(min(all_y) - margin_y, max(all_y) + margin_y)
    ax.set_aspect('equal', adjustable='box')

    # Noktanın değerini okumak için mplcursors kullanımı
    cursor = mplcursors.cursor(hover=True)

    @cursor.connect("add")
    def on_add(sel):
        x, y = sel.target
        sel.annotation.set_text(f"x={x:.2f}\ny={y:.2f}")

    # Grafik ayarları
    ax.legend()
    ax.set_title("Poligon Nokta-İçinde-Poligon Testi Görselleştirmesi")
    ax.set_xlabel("X Koordinatı")
    ax.set_ylabel("Y Koordinatı")
    ax.grid(True, alpha=0.3)
    plt.tight_layout()
    plt.show()

def main():
    """Main function"""
    print("Poligon ve test noktası verileri okunuyor...")
    
    try:
        polygon_points, test_points, results = read_results_file()
        print(f" {len(polygon_points)-1} adet poligon noktası başarıyla yüklendi ve {len(test_points)} test noktası yüklendi.")
        
        # Show summary
        inside_count = sum(results)
        outside_count = len(results) - inside_count
        print(f"Poligon içinde kalan noktalar: {inside_count}")
        print(f"Poligon dışında kalan noktalar: {outside_count}")
        
        
        visualize_polygon_and_points(polygon_points, test_points, results)
        
    except KeyboardInterrupt:
        print("\nKullanıcı tarafından görselleştirme iptal edildi.")
    except Exception as e:
        print(f"Beklenmeyen hata: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()