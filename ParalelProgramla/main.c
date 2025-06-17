#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <stdbool.h>
#include <time.h>
#include <math.h>

#define MAX_POINTS 32
#define MAX_LINE_LENGTH 2048
#define MAX_THREADS 16

#ifndef M_PI
#define M_PI 3.14159265358979
#endif

// Structures for points, bounding boxes, and thread data
typedef struct {
    double x, y;
} Point;

typedef struct {
    double min_x, max_x, min_y, max_y;
} BoundingBox;

typedef struct {
    Point* polygon;
    int n;
    Point* test_points;
    int start_index;
    int end_index;
    bool* results;
    int thread_id;
    BoundingBox* bbox;
} ThreadData;

typedef struct {
    LARGE_INTEGER start, end, frequency;
} Timer;

// Global variables
HANDLE mutex;
CRITICAL_SECTION rand_cs;
bool rand_cs_initialized = false;
FILE* global_file = NULL;

// Function prototypes
void generate_random_polygon(Point* polygon, int n);
bool generate_convex_polygon_safe(Point* polygon, int n);
void generate_concave_polygon(Point* polygon, int n);
int parse_test_points(char* input, Point* test_points, int max_points);
bool optimized_point_in_polygon(Point* polygon, int n, Point p, BoundingBox* box);
int input_int_in_range(const char* prompt, int min, int max);
void input_point(int index, Point* p);
bool validate_test_points_input(const char* input);
BoundingBox calculate_bounding_box(Point* polygon, int n);
bool point_in_bounding_box(Point p, BoundingBox box);
void process_points_parallel(Point* polygon, int n, Point* test_points, int test_count);
void init_thread_safe_rand();
void cleanup_thread_safe_rand();
int thread_safe_rand();
void start_timer(Timer* timer);
double end_timer(Timer* timer);

// Thread functions
DWORD WINAPI parallel_point_test(LPVOID param);

// Utility functions for sorting
int compare_doubles(const void* a, const void* b);
int compare_vectors_by_angle(const void* a, const void* b);

// ------------------
// Thread-safe random number generation
// ------------------

void init_thread_safe_rand() {
    if (!rand_cs_initialized) {
        InitializeCriticalSection(&rand_cs);
        rand_cs_initialized = true;
    }
}

int thread_safe_rand() {
    int result;
    EnterCriticalSection(&rand_cs);
    result = rand();
    LeaveCriticalSection(&rand_cs);
    return result;
}

void cleanup_thread_safe_rand() {
    if (rand_cs_initialized) {
        DeleteCriticalSection(&rand_cs);
        rand_cs_initialized = false;
    }
}

// ------------------
// Performance measurement
// ------------------

void start_timer(Timer* timer) {
    QueryPerformanceFrequency(&timer->frequency);
    QueryPerformanceCounter(&timer->start);
}

double end_timer(Timer* timer) {
    QueryPerformanceCounter(&timer->end);
    return (double)(timer->end.QuadPart - timer->start.QuadPart) / timer->frequency.QuadPart;
}

// ------------------
// Utility functions
// ------------------

int input_int_in_range(const char* prompt, int min, int max) {
    int value;
    char line[MAX_LINE_LENGTH];
    while (true) {
        printf("%s", prompt);
        if (!fgets(line, sizeof(line), stdin)) {
            printf("Girdi okunamadi, tekrar deneyin.\n");
            continue;
        }

        char* endptr;
        value = (int)strtol(line, &endptr, 10);

        if (endptr == line || (*endptr != '\n' && *endptr != '\0')) {
            printf("Gecersiz giris, tam sayi bekleniyor.\n");
            continue;
        }
        if (value < min || value > max) {
            printf("Gecersiz aralik, %d ile %d arasinda olmalidir.\n", min, max);
            continue;
        }
        break;
    }
    return value;
}

void input_point(int index, Point* p) {
    char line[MAX_LINE_LENGTH];
    while (true) {
        printf("%d. noktanin (x y) koordinatlarini girin: ", index + 1);
        if (!fgets(line, sizeof(line), stdin)) {
            printf("Girdi okunamadi, tekrar deneyin.\n");
            continue;
        }
        double x, y;
        int matched = sscanf(line, "%lf %lf", &x, &y);
        if (matched != 2) {
            printf("Gecersiz format, lutfen iki sayi girin.\n");
            continue;
        }
        p->x = x;
        p->y = y;
        break;
    }
}

bool validate_test_points_input(const char* input) {
    int open_brackets = 0, close_brackets = 0;
    for (const char* p = input; *p != '\0'; p++) {
        if (*p == '[') open_brackets++;
        else if (*p == ']') close_brackets++;
    }
    if (open_brackets == 0 || open_brackets != close_brackets) {
        printf("Test noktalarinda kutu parantez sayisi uyusmuyor veya yok.\n");
        return false;
    }
    return true;
}

// ------------------
// Bounding box calculations
// ------------------

BoundingBox calculate_bounding_box(Point* polygon, int n) {
    BoundingBox box;
    box.min_x = box.max_x = polygon[0].x;
    box.min_y = box.max_y = polygon[0].y;
    
    for (int i = 1; i < n; i++) {
        if (polygon[i].x < box.min_x) box.min_x = polygon[i].x;
        if (polygon[i].x > box.max_x) box.max_x = polygon[i].x;
        if (polygon[i].y < box.min_y) box.min_y = polygon[i].y;
        if (polygon[i].y > box.max_y) box.max_y = polygon[i].y;
    }
    
    return box;
}

bool point_in_bounding_box(Point p, BoundingBox box) {
    return (p.x >= box.min_x && p.x <= box.max_x && 
            p.y >= box.min_y && p.y <= box.max_y);
}

// ------------------
// Sorting functions
// ------------------

int compare_doubles(const void* a, const void* b) {
    double da = *(const double*)a;
    double db = *(const double*)b;
    if (da < db) return -1;
    if (da > db) return 1;
    return 0;
}

int compare_vectors_by_angle(const void* a, const void* b) {
    Point* pa = (Point*)a;
    Point* pb = (Point*)b;
    double angleA = atan2(pa->y, pa->x);
    double angleB = atan2(pb->y, pb->x);
    if (angleA < angleB) return -1;
    if (angleA > angleB) return 1;
    return 0;
}

// ------------------
// Enhanced polygon generation
// ------------------

bool generate_convex_polygon_safe(Point* polygon, int n) {
    if (n < 4 || !polygon) {
        printf("Poligon olusturma icin gecersiz parametreler \n");
        return false;
    }
    
    // Bellek ayirma hata kontrolu
    double* xPool = (double*)malloc(n * sizeof(double));
    double* yPool = (double*)malloc(n * sizeof(double));
    double* xVec = (double*)malloc(n * sizeof(double));
    double* yVec = (double*)malloc(n * sizeof(double));
    Point* vectors = (Point*)malloc(n * sizeof(Point));
    
    if (!xPool || !yPool || !xVec || !yVec || !vectors) {
        printf("Poligon olusturma icin bellek ayirmada hata\n");
        // Başarili bellekleri temizle
        if (xPool) free(xPool);
        if (yPool) free(yPool);
        if (xVec) free(xVec);
        if (yVec) free(yVec);
        if (vectors) free(vectors);
        return false;
    }
    
    // Rastgele koordinat havuzunu doldur
    for (int i = 0; i < n; i++) {
        xPool[i] = (double)(thread_safe_rand() % 1000) / 100.0; // Range 0-10
        yPool[i] = (double)(thread_safe_rand() % 1000) / 100.0;
    }
    
    // koordinat havuzunu sirala
    qsort(xPool, n, sizeof(double), compare_doubles);
    qsort(yPool, n, sizeof(double), compare_doubles);
    
    // Extract extremes
    double minX = xPool[0], maxX = xPool[n-1];
    double minY = yPool[0], maxY = yPool[n-1];
    
    // Dengesiz dağılım durumunda min ve max değerleri ayarla
    // Bu, poligonun düzgün bir şekilde kaplanmasını sağlar
    if (fabs(maxX - minX) < 1e-6) {
        maxX = minX + 1.0;
    }
    if (fabs(maxY - minY) < 1e-6) {
        maxY = minY + 1.0;
    }
    
    // Vektörleri bellekte yerini oluştur
    memset(xVec, 0, n * sizeof(double));
    memset(yVec, 0, n * sizeof(double));
    
    // X vektörlerini oluştur - zincirleme atama ile
    double lastTop = minX, lastBot = minX;
    for (int i = 1; i < n - 1; i++) {
        double x = xPool[i];
        if (thread_safe_rand() % 2) { // üst zincir
            xVec[i] = x - lastTop;
            lastTop = x;
        } else { // alt zincir
            xVec[i] = lastBot - x;
            lastBot = x;
        }
    }
    // Zincirleri düzgün kapat
    xVec[0] = maxX - lastTop;
    xVec[n-1] = lastBot - minX;

    // Y vektörlerini oluştur - zincirleme atama ile
    lastTop = minY;
    lastBot = minY;
    for (int i = 1; i < n - 1; i++) {
        double y = yPool[i];
        if (thread_safe_rand() % 2) { // üst zincir
            yVec[i] = y - lastTop;
            lastTop = y;
        } else { // alt zincir
            yVec[i] = lastBot - y;
            lastBot = y;
        }
    }
    // Zincirleri düzgün kapat
    yVec[0] = maxY - lastTop;
    yVec[n-1] = lastBot - minY;

    // X ve Y bileşenlerini rastgele eşleştir - Fisher-Yates shuffle
    for (int i = n - 1; i > 0; i--) {
        int j = thread_safe_rand() % (i + 1);
        double temp = yVec[i];
        yVec[i] = yVec[j];
        yVec[j] = temp;
    }
    
    //Oluşturulan çiftleri vektörlere dönüştür
    for (int i = 0; i < n; i++) {
        vectors[i].x = xVec[i];
        vectors[i].y = yVec[i];
    }
    
    // Vektörleri açılarına göre sırala
    qsort(vectors, n, sizeof(Point), compare_vectors_by_angle);
    
    // vektörleri ucuca ekle
    polygon[0].x = 0;
    polygon[0].y = 0;
    for (int i = 1; i < n; i++) {
        polygon[i].x = polygon[i-1].x + vectors[i-1].x;
        polygon[i].y = polygon[i-1].y + vectors[i-1].y;
    }
    
    //Ölçeklendirme için mevcut sınırlar hesaplanır
    double currentMinX = polygon[0].x, currentMaxX = polygon[0].x;
    double currentMinY = polygon[0].y, currentMaxY = polygon[0].y;
    
    for (int i = 1; i < n; i++) {
        if (polygon[i].x < currentMinX) currentMinX = polygon[i].x;
        if (polygon[i].x > currentMaxX) currentMaxX = polygon[i].x;
        if (polygon[i].y < currentMinY) currentMinY = polygon[i].y;
        if (polygon[i].y > currentMaxY) currentMaxY = polygon[i].y;
    }
    
    // Ölçeklendirmeleri mevcut sınırlar ile hedef sınırlar arasında hesapla
    double currentWidth = currentMaxX - currentMinX;
    double currentHeight = currentMaxY - currentMinY;
    
    if (currentWidth < 1e-10) currentWidth = 1.0;
    if (currentHeight < 1e-10) currentHeight = 1.0;
    
    double targetWidth = maxX - minX;
    double targetHeight = maxY - minY;
    double scaleX = targetWidth / currentWidth;
    double scaleY = targetHeight / currentHeight;
    
    for (int i = 0; i < n; i++) {
        polygon[i].x = minX + (polygon[i].x - currentMinX) * scaleX;
        polygon[i].y = minY + (polygon[i].y - currentMinY) * scaleY;
    }
   
    // Bellek sızıntısını önlemek için tüm dinamik bellekleri serbest bırak
    free(xPool);
    free(yPool);
    free(xVec);
    free(yVec);
    free(vectors);
    
    return true;
}

void generate_concave_polygon(Point* polygon, int n) {
    if (n < 4) return;
    
    Point center = {10.0, 10.0};
    double outer_radius = 25.0;
    double inner_radius = 10.0;
    
    // İç ve dış çaplar arasında rastgele bir dağılım oluştur
    for (int i = 0; i < n; i++) {
        double angle = (2.0 * M_PI * i) / n;
        double radius;
        
        // Öteki noktaları dağıt (Konkavlık oluşturmak için)
        if (i % 2 == 0) {
            radius = outer_radius + (thread_safe_rand() % 10) - 5; // Dış noktalar için varyasyon
        } else {
            radius = inner_radius + (thread_safe_rand() % 10) - 5; // İç noktalar için varyasyon
        }
        
        if (radius < 3) radius = 3; // minimum değerin altında kalmaması için düzeltme
        
        polygon[i].x = center.x + radius * cos(angle);
        polygon[i].y = center.y + radius * sin(angle);
    }
}

void generate_random_polygon(Point* polygon, int n) {
    int type = input_int_in_range("Poligon tipi secin:\n1 - Konveks\n2 - Konkav\nSeciminiz: ", 1, 2);
    if (type == 1) {
        if (!generate_convex_polygon_safe(polygon, n)) {
            printf("Konveks poligon olusturulamadi, varsayilan poligon kullaniliyor.\n");
            // Fallback to simple triangle
            polygon[0].x = 0; polygon[0].y = 0;
            polygon[1].x = 10; polygon[1].y = 0;
            polygon[2].x = 5; polygon[2].y = 10;
        }
    } else {
        generate_concave_polygon(polygon, n);
    }
}

// ------------------
// Optimized point-in-polygon test
// ------------------

bool optimized_point_in_polygon(Point* polygon, int n, Point p, BoundingBox* box) {
    // Quick rejection test
    if (!point_in_bounding_box(p, *box)) {
        return false;
    }
    
    // Standard ray casting algoritması
    int count = 0;
    for (int i = 0; i < n; i++) {
        Point a = polygon[i];
        Point b = polygon[(i + 1) % n];
        
        if (((a.y > p.y) != (b.y > p.y)) &&
            (p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x)) {
            count++;
        }
    }
    return count % 2 == 1;
}

// ------------------
// Parallel processing
// ------------------

DWORD WINAPI parallel_point_test(LPVOID param) {
    ThreadData* data = (ThreadData*)param;
    
    // Each thread processes a subset of test points
    for (int i = data->start_index; i < data->end_index; i++) {
        data->results[i] = optimized_point_in_polygon(data->polygon, data->n, data->test_points[i], data->bbox);
        
        // Thread-safe output
        WaitForSingleObject(mutex, INFINITE);
        if (global_file) {
            fprintf(global_file, "%.2lf %.2lf => %s\n",
                data->test_points[i].x, data->test_points[i].y,
                data->results[i] ? "EVET" : "HAYIR");
        }
        ReleaseMutex(mutex);
    }
    
    return 0;
}

void process_points_parallel(Point* polygon, int n, Point* test_points, int test_count) {
    HANDLE threads[MAX_THREADS];
    ThreadData thread_data[MAX_THREADS];
    bool* results = (bool*)malloc(test_count * sizeof(bool));
    BoundingBox bbox = calculate_bounding_box(polygon, n);
    
    if (!results) {
        printf("Memory allocation failed for results!\n");
        return;
    }
    
    global_file = fopen("results.txt", "w");
    if (!global_file) {
        printf("Cannot open results file!\n");
        free(results);
        return;
    }
    
    // Write polygon info
    fprintf(global_file, "Polygon Points (count: %d):\n", n);
    for (int i = 0; i < n; i++) {
        fprintf(global_file, "%.2lf %.2lf\n", polygon[i].x, polygon[i].y);
    }
    fprintf(global_file, "\nTest Points:\n");
    
    // Divide work among threads
    int points_per_thread = test_count / MAX_THREADS;
    int remaining_points = test_count % MAX_THREADS;
    int active_threads = 0;
    
    for (int i = 0; i < MAX_THREADS && i * points_per_thread < test_count; i++) {
        thread_data[i].polygon = polygon;
        thread_data[i].n = n;
        thread_data[i].test_points = test_points;
        thread_data[i].results = results;
        thread_data[i].thread_id = i;
        thread_data[i].bbox = &bbox;
        
        thread_data[i].start_index = i * points_per_thread;
        thread_data[i].end_index = (i + 1) * points_per_thread;
        
        // Last thread handles remaining points
        if (i == MAX_THREADS - 1) {
            thread_data[i].end_index += remaining_points;
        }
        
        // Only create thread if there are points to process
        if (thread_data[i].start_index < test_count) {
            threads[i] = CreateThread(NULL, 0, parallel_point_test, &thread_data[i], 0, NULL);
            if (threads[i] != NULL) {
                active_threads++;
            } else {
                printf("Failed to create thread %d\n", i);
            }
        } else {
            threads[i] = NULL;
        }
    }
    
    // Wait for all threads to complete
    for (int i = 0; i < active_threads; i++) {
        if (threads[i] != NULL) {
            WaitForSingleObject(threads[i], INFINITE);
            CloseHandle(threads[i]);
        }
    }
    
    fclose(global_file);
    global_file = NULL;
    free(results);
}

// ------------------
// Benchmarking
// ------------------



// ------------------
// Input parsing
// ------------------

int parse_test_points(char* input, Point* test_points, int max_points) {
    int count = 0;
    char* ptr = input;
    while (*ptr && count < max_points) {
        while (*ptr && *ptr != '[') ptr++;
        if (!*ptr) break;
        double x, y;
        if (sscanf(ptr, "[%lf,%lf]", &x, &y) == 2) {
            test_points[count].x = x;
            test_points[count].y = y;
            count++;
        }
        ptr++;
    }
    return count;
}

// ------------------
// Main function
// ------------------

int main() {
    
    srand((unsigned int)time(NULL));
    init_thread_safe_rand();
    
    Point polygon[MAX_POINTS];
    Point test_points[MAX_POINTS];
    int n, choice, test_count;
    char buffer[MAX_LINE_LENGTH];

    mutex = CreateMutex(NULL, FALSE, NULL);
    if (mutex == NULL) {
        printf("Mutex olusturulamadi!\n");
        cleanup_thread_safe_rand();
        return 1;
    }

        
    printf("=== Paralel Nokta-Polygon-Icinde/Disinda Test Uygulamasi ===\n\n");

    choice = input_int_in_range("Poligon icin secim yapin:\n1 - Noktalari elle gir\n2 - Rastgele poligon olustur\nSeciminiz: ", 1, 2);
    n = input_int_in_range("Poligonun nokta sayisini girin (min 3, max 32): ", 3, MAX_POINTS);
    if (choice == 1) {
       
        for (int i = 0; i < n; i++) {
            input_point(i, &polygon[i]);
        }
    }
    else {
       
        generate_random_polygon(polygon, n);

        printf("Olusturulan rastgele poligon noktalari:\n");
        for (int i = 0; i < n; i++) {
            printf("[%.2lf, %.2lf]", polygon[i].x, polygon[i].y);
            if (i < n - 1) printf(", ");
            if ((i + 1) % 5 == 0) printf("\n"); // 5 points per line
        }
        printf("\n");
    }

    while (true) {
        printf("\nTest noktalarini girin ([x1,y1],[x2,y2],...):\n");
        if (!fgets(buffer, sizeof(buffer), stdin)) {
            printf("Girdi okunamadi, tekrar deneyin.\n");
            continue;
        }
        if (!validate_test_points_input(buffer)) {
            printf("Lutfen test noktalarini belirtilen formata uygun girin.\n");
            continue;
        }

        test_count = parse_test_points(buffer, test_points, MAX_POINTS);
        if (test_count == 0) {
            printf("En az bir test noktasi girmeniz gerekiyor.\n");
            continue;
        }
        break;
    }

    printf("\nIsleniyor... (Paralel hesaplama kullaniliyor)\n");

    // Benchmark the operation
    Timer total_timer;
    start_timer(&total_timer);

    // Process points using parallel threads
    process_points_parallel(polygon, n, test_points, test_count);

    double total_time = end_timer(&total_timer);

    printf("Islem tamamlandi! Toplam sure: %.4f saniye\n", total_time);
    printf("Sonuclar results.txt dosyasina yazildi.\n");


    // Launch visualization
    printf("\nGorselestirme scripti calistiriliyor...\n");
    int result = system("python visualize.py");
    if (result != 0) {
        printf("Gorselestirme scripti calistirilamadi. Lutfen Python yuklu oldugundan emin olun.\n");
        printf("Alternatif olarak: python visualize.py --input results.txt\n");
    }

    // Cleanup
    CloseHandle(mutex);
    cleanup_thread_safe_rand();

    printf("\nUygulama tamamlandi. Cikmak icin bir tusa basin...\n");
    getchar();

    return 0;
}