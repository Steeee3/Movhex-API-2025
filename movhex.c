#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define WHITE 0
#define GREY 1
#define BLACK 2

#define max(a,b) ((a) > (b) ? (a) : (b))

/*Coordinates*/
typedef struct {
    uint32_t x;
    uint32_t y;
} Offset;

typedef struct {
    uint32_t r;
    int32_t q;
} Axial;

/*Utils*/
static inline void swap(uint32_t *x1, uint32_t *x2) {
    uint32_t temp = *x1;
    *x1 = *x2;
    *x2 = temp;
}

/*Grid and hexagons*/

typedef struct {
    /* ------------- hexagon data ------------- */
    uint32_t landCost;
    uint8_t airRoutesNum;
    uint32_t airRoutes[5];
    uint32_t airRoutesCost[5];
    uint8_t color;

    /* ------------- for fast queries ------------- */
    uint32_t distance;
    uint32_t predecessor;
    uint32_t version;
} Hex;

Hex* grid = NULL;
uint32_t columnsSize = 0;
uint32_t rowsSize = 0;
uint32_t size = 0;
uint32_t currentVersion = 0;

typedef struct {
    Axial adj[6];
    uint8_t size;
} Adjacents;

typedef struct {
    uint32_t adj[6];
    uint8_t size;
} AdjacentsLinear;
static const int8_t dR[6] = {  0,  0, +1, -1, -1, +1 };
static const int8_t dQ[6] = { +1, -1,  0,  0, +1, -1 };

static const int8_t dxEven[6] = { +1,  0, -1, -1, -1,  0 };
static const int8_t dyEven[6] = {  0, -1, -1,  0, +1, +1 };

static const int8_t dxOdd[6]  = { +1, +1,  0, -1,  0, +1 };
static const int8_t dyOdd[6]  = {  0, -1, -1,  0, +1, +1 };

uint32_t (*adjacencyMap)[6];

//Coordinates conversion
static inline int linearToX(uint32_t idx);
static inline int linearToY(uint32_t idx);
static inline uint32_t offsetToLinear(int32_t x, int32_t y);
static inline uint32_t axialToLinear(int32_t r, int32_t q);
static inline Axial offsetToAxial(uint32_t x, uint32_t y);
static inline Offset axialToOffset(uint32_t r, int32_t q);
static inline Offset linearToOffset(uint32_t index);

static inline AdjacentsLinear findAdjacentsOffset(int32_t x, int32_t y) {
    AdjacentsLinear adjacents;
    adjacents.size = 0;

    const int8_t *dx = (y & 1) ? dxOdd  : dxEven;
    const int8_t *dy = (y & 1) ? dyOdd  : dyEven;

    for (int i = 0; i < 6; i++) {
        int32_t adjX = x + dx[i];
        int32_t adjY = y + dy[i];

        if (adjX < 0 || adjY < 0 || adjX >= columnsSize || adjY >= rowsSize) {
            continue;
        }

        adjacents.adj[adjacents.size] = (uint32_t) (adjY * columnsSize + adjX);
        adjacents.size++;
    }
    return adjacents;
}

static inline Adjacents findAdjacents(Axial source) {
    Adjacents adj;
    Axial hex;

    adj.size = 0;
    
    for (int i = 0; i < 6; i++) {
        hex.r = source.r + dR[i];
        hex.q = source.q + dQ[i];

        uint32_t index = axialToLinear(hex.r, hex.q);

        if (index != UINT32_MAX) {
            adj.adj[adj.size] = hex;
            adj.size++;
        }
    }

    return adj;
}

/*Data structures*/
typedef struct {
    Axial* data;
    uint16_t head;
    uint16_t tail;
    uint16_t size;
} AxialQueue;
typedef struct {
    Hex* data;
    uint16_t head;
    uint16_t tail;
    uint16_t size;
} HexQueue;

AxialQueue newAxialQueue() {
    AxialQueue queue;

    queue.data = malloc(size * sizeof(Axial));
    queue.size = 0;
    queue.head = 0;
    queue.tail = 0;

    return queue;
}

static inline int enqueueAxial(AxialQueue* queue, Axial element) {
    if (queue->size == size) {
        return -1;
    }

    queue->data[queue->tail] = element;
    queue->tail = (queue->tail + 1) % size;
    queue->size++;
    return 0;
}

static inline Axial dequeueAxial(AxialQueue* queue) {
    Axial element = queue->data[queue->head];
    queue->head = (queue->head + 1) % size;
    queue->size--;

    return element;
}

static inline int isEmptyAxialQueue(AxialQueue* queue) {
    return queue->size == 0;
}

static inline void freeAxialQueue(AxialQueue* queue) {
    free(queue->data);
}

HexQueue newHexQueue() {
    HexQueue queue;

    queue.data = malloc(size * sizeof(Hex));
    queue.size = 0;
    queue.head = 0;
    queue.tail = 0;

    return queue;
}

static inline int enqueueHex(HexQueue* queue, Hex element) {
    if (queue->size == size) {
        return -1;
    }

    queue->data[queue->tail] = element;
    queue->tail = (queue->tail + 1) % size;
    queue->size++;
    return 0;
}

static inline Hex dequeueHex(HexQueue* queue) {
    Hex element = queue->data[queue->head];
    queue->head = (queue->head + 1) % size;
    queue->size--;

    return element;
}

static inline int isEmptyHexQueue(HexQueue* queue) {
    return queue->size == 0;
}

typedef struct {
    uint32_t *data;
    uint32_t size;
} IndexHeap;


#define LEFT(i)   (2*i + 1)
#define RIGHT(i)  (2*i +2)
#define PARENT(i) ((i - 1) / 2)

static inline IndexHeap newIndexHeap() {
    IndexHeap heap;

    heap.data = malloc(size * sizeof(uint32_t));
    heap.size = 0;

    return heap;
}

static inline void insertIndexHeap(IndexHeap *heap, uint32_t key) {
    heap->data[heap->size] = key;
    heap->size++;

    uint32_t i = heap->size - 1;
    while (i > 0 && grid[heap->data[i]].distance < grid[heap->data[PARENT(i)]].distance) {
        swap(&heap->data[PARENT(i)], &heap->data[i]);
        i = PARENT(i);
    }
}

static inline void minIndexHeapify(IndexHeap *heap, uint32_t n) {
    uint32_t left = LEFT(n);
    uint32_t right = RIGHT(n);

    uint32_t minPos;
    if (left < heap->size && grid[heap->data[left]].distance < grid[heap->data[n]].distance) {
        minPos = left;
    } else {
        minPos = n;
    }
    if (right < heap->size && grid[heap->data[right]].distance < grid[heap->data[minPos]].distance) {
        minPos = right;
    }

    if (minPos != n) {
        swap(&heap->data[n], &heap->data[minPos]);
        minIndexHeapify(heap, minPos);
    }
}

static inline uint32_t removeIndexHeap(IndexHeap *heap) {
    if (heap->size < 1) {
        return UINT32_MAX;
    }

    uint32_t min = heap->data[0];
    heap->size--;
    heap->data[0] = heap->data[heap->size];
    minIndexHeapify(heap, 0);
    return min;
}

static inline uint32_t minIndexHeap(IndexHeap *heap) {
    return heap->data[0];
}

static inline void indexHeapDecreaseKey(IndexHeap *heap, uint32_t i, uint32_t cost) {
    if (cost >= grid[heap->data[i]].distance) {
        return;
    }

    grid[heap->data[i]].distance = cost;
    while (i > 0 && grid[heap->data[i]].distance < grid[heap->data[PARENT(i)]].distance) {
        swap(&heap->data[PARENT(i)], &heap->data[i]);
        i = PARENT(i);
    }
}

static inline int isEmptyIndexHeap(IndexHeap* heap) {
    return heap->size == 0;
}

static inline void freeIndexHeap(IndexHeap *heap)
{
    if (!heap) {
        return;
    }

    free(heap->data);
    heap->data = NULL;
    heap->size = 0;
}

/*Wrapping and unwrapping a 16b into a 32b int*/
static inline uint32_t wrap(uint16_t high, uint16_t low) {
    return (high << 16) | low;
}

static inline uint16_t unwrapHigh(uint32_t data) {
    return data >> 16;
}

static inline uint16_t unwrapLow(uint32_t data) {
    return data & 0xFFFF;
}

/*Operations*/
void init(uint32_t columns, uint32_t rows);
void changeCost(int x, int y, int8_t param, uint16_t radius);
void toggleAirRoute(int32_t x1, int32_t y1, int32_t x2, int32_t y2);
void travelCost(int32_t x1, int32_t y1, int32_t x2, int32_t y2);

/*Prototype*/
//Input functions
static void dispatchInput(const char *line);

//init support functions
uint_fast8_t isOutOfBounds(uint32_t columns, uint32_t rows);
Hex *newGrid(uint32_t columns, uint32_t rows);
void initializeGridCosts();
static inline void fillAdjacencyMap();

//changeCost support functions
void changeHexCost(Hex* hexagon, int8_t param, uint16_t radius);
static inline int floorDiv(int a, int b);

//toggleAirRoute support functions
static inline void activateAirRoute(uint32_t hex1Index, uint32_t hex2Index, int32_t sum);
static inline void removeAirRoute(uint32_t hex1Index, uint32_t hex2Index, uint8_t position);
static inline void swap(uint32_t *x1, uint32_t *x2);

//?TEST ONLY
static inline void printGrid();

int main() {
    char buf[256];

    while (fgets(buf, sizeof buf, stdin)) {
        dispatchInput(buf);
    }
    free(grid);
    return 0;
}

static void dispatchInput(const char *line){
    char cmd[16];

    if(sscanf(line, "%15s", cmd) != 1) {
        return;
    }

    switch(cmd[0]){
    case 'i':
        if(strcmp(cmd, "init")==0) {
            uint32_t c, r;

            if (sscanf(line, "%*s %u %u", &c, &r) == 2) {
                init(c, r);
            }
        }
        break;
    case 'c':
        if(strcmp(cmd, "change_cost") == 0 ){
            int x, y;
            int p;
            unsigned rad;

            if (sscanf(line, "%*s %d %d %d %u", &x, &y, &p, &rad) == 4) {
                changeCost(x,y,(int8_t)p,(uint16_t)rad);
            }
        }
        break;
    case 't':
        if(cmd[2]=='g'){
            int x1, y1, x2, y2;

            if (sscanf(line, "%*s %d %d %d %d", &x1, &y1, &x2, &y2) == 4) {
                toggleAirRoute(x1,y1,x2,y2);
            }
        } else {
            int x1, y1, x2, y2;

            if (sscanf(line, "%*s %d %d %d %d", &x1, &y1, &x2, &y2) == 4){
                travelCost(x1,y1,x2,y2);
            }
        }
        break;
    default:
        fprintf(stderr,"KO: %s not a command\n",cmd);
    }
}

static inline void printGrid()
{
    for (uint32_t row = 0; row < rowsSize; ++row) {

        /* se la riga è dispari, sfalsala di due spazi */
        if (row & 1) printf("  ");

        for (uint32_t col = 0; col < columnsSize; ++col) {
            uint32_t idx = row * columnsSize + col;
            printf("%2u ", grid[idx].landCost);   /* due cifre allineate */
        }
        putchar('\n');
    }
}

/*Coordinates conversion*/
static inline int linearToX(uint32_t idx) {
    return idx % columnsSize;
}

static inline int linearToY(uint32_t idx) {
    return idx / columnsSize;
}

static inline uint32_t offsetToLinear(int32_t x, int32_t y) {
    if((unsigned) x >= columnsSize || (unsigned) y >= rowsSize) {
        return UINT32_MAX;
    }
    return (uint32_t) y * columnsSize + x;
}

static inline uint32_t axialToLinear(int32_t r, int32_t q) {
    Offset coord = axialToOffset((uint32_t) r, (int32_t) q);

    return offsetToLinear(coord.x, coord.y);
}

static inline Axial linearToAxial(uint32_t index) {
    Axial coord;

    uint32_t y = index / columnsSize;
    uint32_t x = index % columnsSize;

    coord.r = y;
    coord.q =  (int32_t)x - (int32_t)(y >> 1);

    return coord;
}

static inline Axial offsetToAxial(uint32_t x, uint32_t y) {
    Axial newCoord;

    newCoord.r = y;
    newCoord.q = (int32_t) x - (y >> 1);

    return newCoord;
}

static inline Offset axialToOffset(uint32_t r, int32_t q) {
    Offset newCoord;

    newCoord.x = (uint32_t) q + (r >> 1);
    newCoord.y = r;

    return newCoord;
}

static inline Offset linearToOffset(uint32_t index) {
    Offset coord;

    coord.x = linearToX(index);
    coord.y = linearToY(index);
    return coord;
}

void init(uint32_t columns, uint32_t rows) {
    if (isOutOfBounds(columns, rows)) {
        fprintf(stderr, "KO\n");
        exit(EXIT_FAILURE);
    }

    free(grid);
    grid = newGrid(columns, rows);

    initializeGridCosts();

    adjacencyMap = malloc(size * sizeof(*adjacencyMap));
    fillAdjacencyMap();

    printf("OK\n");
}
uint_fast8_t isOutOfBounds(uint32_t columns, uint32_t rows) {
    return columns == 0 || rows == 0 || columns > (1u<<20) || rows > (1u<<20);
}
Hex *newGrid(uint32_t columns, uint32_t rows) {
    Hex *temp = malloc(columns * rows * sizeof(Hex));

    if (!temp) {
        perror("malloc failed");
        exit(EXIT_FAILURE);
    }

    columnsSize = columns;
    rowsSize = rows;
    size = columns * rows;

    return temp;
}
void initializeGridCosts() {
    for (int i = 0; i < size; i++) {
        grid[i].landCost = 1;
        grid[i].airRoutesNum = 0;
    }
}

static inline void fillAdjacencyMap() {
    for (int32_t y = 0; y < (int32_t) rowsSize; y++) {
        const int8_t *dx = (y & 1) ? dxOdd : dxEven;
        const int8_t *dy = (y & 1) ? dyOdd : dyEven;

        for (int32_t x = 0; x < (int32_t) columnsSize; x++) {
            uint32_t index = (uint32_t) (y * columnsSize + x);

            for (int i = 0; i < 6; i++) {
                int32_t adjX = x + dx[i];
                int32_t adjY = y + dy[i];

                adjacencyMap[index][i] = offsetToLinear(adjX, adjY);
            }
        }
    }
}

void changeCost(int x, int y, int8_t param, uint16_t radius) {
    Axial coord = offsetToAxial((uint32_t) x, (uint32_t) y);
    int sourceIndex = axialToLinear(coord.r, coord.q);

    if (radius == 0 || param < -10 || param > 10 || sourceIndex == UINT32_MAX) {
        printf("KO\n");
        return;
    } else {
        printf("OK\n");
    }

    currentVersion++;

    for (int i = 0; i < size; i++) {
        if (i == sourceIndex) {
            continue;
        }

        grid[i].color = WHITE;
        grid[i].distance = UINT16_MAX;
    }

    grid[sourceIndex].color = GREY;
    grid[sourceIndex].distance = 0;
    changeHexCost(&grid[sourceIndex], param, radius);

    AxialQueue queue = newAxialQueue();
    enqueueAxial(&queue, coord);

    Axial currentAxial;
    while (!isEmptyAxialQueue(&queue)) {
        currentAxial = dequeueAxial(&queue);
        uint32_t currentIndex = axialToLinear(currentAxial.r, currentAxial.q);
        Hex *currentHex = &grid[currentIndex];

        if (currentHex->distance >= radius) {
            continue;
        }

        Adjacents adj = findAdjacents(currentAxial);
        for (int i = 0; i < adj.size; i++) {
            Hex *adjHex = &grid[axialToLinear(adj.adj[i].r, adj.adj[i].q)];

            if (adjHex->color == WHITE) {
                adjHex->color = GREY;
                adjHex->distance = currentHex->distance + 1;
                enqueueAxial(&queue, adj.adj[i]);

                changeHexCost(adjHex, param, radius);
            }
        }
        currentHex->color = BLACK;
    }
    freeAxialQueue(&queue);
}
void changeHexCost(Hex* hexagon, int8_t param, uint16_t radius) {
    int num = param * (radius - hexagon->distance);
    int delta = floorDiv(num, radius);

    if (delta == 0) {
        return;
    }

    int newCost = hexagon->landCost + delta;

    if (newCost < 0) {
        hexagon->landCost = 0;
    } else if (newCost > 100) {
        hexagon->landCost = 100;
    } else {
        hexagon->landCost = newCost;
    }

    for (int i = 0; i < hexagon->airRoutesNum; i++) {
        newCost = hexagon->airRoutesCost[i] + delta;

        if (newCost >= 0 && newCost <= 100) {
            hexagon->airRoutesCost[i] = newCost;
        }

        if (newCost <= 0) {
            hexagon->airRoutesCost[i] = 1;
        } else if (newCost > 100) {
            hexagon->airRoutesCost[i] = 100;
        } else {
            hexagon->airRoutesCost[i] = newCost;
        }
    }
}
static inline int floorDiv(int a, int b)
{
    return (a >= 0) ?  a / b : -(( -a + b-1) / b);
}

void toggleAirRoute(int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    uint32_t hex1Index = offsetToLinear(x1, y1);
    uint32_t hex2Index = offsetToLinear(x2, y2);

    if (hex1Index == UINT32_MAX || hex2Index == UINT32_MAX || grid[hex1Index].airRoutesNum == 5) {
        printf("KO\n");
        return;
    }
    printf("OK\n");

    int32_t sum = grid[hex1Index].landCost;
    for (int i = 0; i < grid[hex1Index].airRoutesNum; i++) {
        if (grid[hex1Index].airRoutes[i] == hex2Index) {
            sum += grid[hex1Index].airRoutesCost[i];
            removeAirRoute(hex1Index, hex2Index, i);
            return;
        }
    }
    activateAirRoute(hex1Index, hex2Index, sum);
}

static inline void activateAirRoute(uint32_t hex1Index, uint32_t hex2Index, int32_t sum) {
    int32_t average;

    average = sum / (grid[hex1Index].airRoutesNum + 1);

    grid[hex1Index].airRoutes[grid[hex1Index].airRoutesNum] = hex2Index;
    grid[hex1Index].airRoutesCost[grid[hex1Index].airRoutesNum] = average;
    grid[hex1Index].airRoutesNum++;
}

static inline void removeAirRoute(uint32_t hex1Index, uint32_t hex2Index, uint8_t position) {
    for (int i = position; i < grid[hex1Index].airRoutesNum - 1; i++) {
        swap(&grid[hex1Index].airRoutes[i], &grid[hex1Index].airRoutes[i + 1]);
        swap(&grid[hex1Index].airRoutesCost[i], &grid[hex1Index].airRoutesCost[i + 1]);
    }

    grid[hex1Index].airRoutesNum--;
}

void travelCost(int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    currentVersion++;

    uint32_t hex1Index = offsetToLinear(x1, y1);
    uint32_t hex2Index = offsetToLinear(x2, y2);

    if (hex1Index == UINT32_MAX || hex2Index == UINT32_MAX) {
        printf("-1\n");
        return;
    }
    if (grid[hex1Index].landCost == 0) {
        printf("-1\n");
        return;
    }
    if (hex1Index == hex2Index) {
        printf("0\n");
        return;
    }

    IndexHeap heap = newIndexHeap();
    grid[hex1Index].distance = 0;
    grid[hex1Index].color = WHITE;
    grid[hex1Index].version = currentVersion;
    insertIndexHeap(&heap, hex1Index);

    while (!isEmptyIndexHeap(&heap)) {
        uint32_t hexIndex = removeIndexHeap(&heap);

        if (hexIndex == hex2Index) {
            break;
        }

        if (grid[hexIndex].color == BLACK) {
            continue;
        }
        grid[hexIndex].color = BLACK;

        if (grid[hexIndex].landCost == 0) {
            continue;
        }
        
        const uint32_t *adjacents = adjacencyMap[hexIndex];
        uint32_t newDistance = grid[hexIndex].distance + grid[hexIndex].landCost;
        for (int i = 0; i < 6; i++) {
            if (adjacents[i] == UINT32_MAX) {
                continue;
            }

            uint32_t adjIndex = adjacents[i];
            
            if (grid[adjIndex].version != currentVersion) {
                grid[adjIndex].distance = UINT32_MAX;
                grid[adjIndex].color = WHITE;
                grid[adjIndex].version = currentVersion;
            }

            if (newDistance < grid[adjIndex].distance) {
                grid[adjIndex].distance = newDistance;
                grid[adjIndex].predecessor = hexIndex;

                if (grid[adjIndex].color != BLACK) {
                    insertIndexHeap(&heap, adjIndex);
                }
            }
        }

        for (int i = 0; i < grid[hexIndex].airRoutesNum; i++) {
            newDistance = grid[hexIndex].distance + grid[hexIndex].airRoutesCost[i];

            uint32_t adjIndex = grid[hexIndex].airRoutes[i];
            if (grid[adjIndex].version != currentVersion) {
                grid[adjIndex].distance = UINT32_MAX;
                grid[adjIndex].color = WHITE;
                grid[adjIndex].version = currentVersion;
            }

            if (newDistance < grid[adjIndex].distance) {
                grid[adjIndex].distance = newDistance;
                grid[adjIndex].predecessor = hexIndex;

                if (grid[adjIndex].color != BLACK) {
                    insertIndexHeap(&heap, adjIndex);
                }
            }
        }
    }
    
    if (grid[hex2Index].distance == UINT32_MAX || grid[hex2Index].version != currentVersion) {
        printf("-1\n");
    } else {
        printf("%u\n", grid[hex2Index].distance);
    }
    freeIndexHeap(&heap);
}