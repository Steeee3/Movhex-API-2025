#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define WHITE 0
#define GREY 1
#define BLACK 2

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
    /*hexagon data*/
    uint32_t airRoutes[5];
    /*for fast queries*/
    uint32_t distance;
    uint32_t version;
    /*for bucket*/
    uint32_t bucketNext;

    /*hexagon data*/
    uint8_t landCost;
    uint8_t airRoutesNum;
    uint8_t color;
    /*for bucket*/
    uint8_t bucketIndex;
    uint8_t bucketVersion;
} Hex;

Hex* grid = NULL;
uint32_t columnsSize = 0;
uint32_t rowsSize = 0;
uint32_t size = 0;
uint32_t currentVersion = 0;

/*Packing and depacking*/
static inline void setAirRoute(Hex* hexagon, uint8_t routeIndex, uint32_t destination, uint8_t cost) {
    hexagon->airRoutes[routeIndex] = (destination << 8) | cost;
}

static inline void setAirRouteCost(Hex* hexagon, uint8_t routeIndex, uint8_t cost) {
    hexagon->airRoutes[routeIndex] = (hexagon->airRoutes[routeIndex] & 0xFFFFFF00u) | cost;
}

static inline uint32_t getAirRouteDestination(Hex* hexagon, uint8_t routeIndex) {
    return (hexagon->airRoutes[routeIndex] >> 8);
}

static inline uint32_t getAirRouteCost(Hex* hexagon, uint8_t routeIndex) {
    return (uint8_t) hexagon->airRoutes[routeIndex];
}

typedef struct {
    Axial adj[6];
    uint8_t size;
} Adjacents;

static const int8_t dR[6] = {  0,  0, +1, -1, -1, +1 };
static const int8_t dQ[6] = { +1, -1,  0,  0, +1, -1 };

static const int8_t dxEven[6] = { +1,  0, -1, -1, -1,  0 };
static const int8_t dyEven[6] = {  0, -1, -1,  0, +1, +1 };

static const int8_t dxOdd[6]  = { +1, +1,  0, -1,  0, +1 };
static const int8_t dyOdd[6]  = {  0, -1, -1,  0, +1, +1 };

static int32_t dEven[6];
static int32_t dOdd[6];

static inline void initializeDeltaForAdjacents() {
    for (uint8_t i = 0; i < 6; i++) {
        dEven[i] = dyEven[i] * columnsSize + dxEven[i];
        dOdd[i] = dyOdd[i] * columnsSize + dxOdd[i];
    }
}

//Coordinates conversion
static inline int linearToX(uint32_t idx);
static inline int linearToY(uint32_t idx);
static inline uint32_t offsetToLinear(int32_t x, int32_t y);
static inline uint32_t axialToLinear(int32_t r, int32_t q);
static inline Axial offsetToAxial(uint32_t x, uint32_t y);
static inline Offset axialToOffset(uint32_t r, int32_t q);
static inline Offset linearToOffset(uint32_t index);

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

#define BUCKET_SIZE 101
typedef struct {
    uint32_t head[BUCKET_SIZE];
    uint32_t current;
    uint32_t distance;
    uint32_t count;
} Bucket;

Bucket bucket;
uint8_t bucketVersion = 0;

static inline void initializeBucket() {
    bucketVersion++;

    if (bucketVersion == 0) {
        for (uint32_t i = 0; i < size; i++) {
            grid[i].bucketVersion = 0;
        }
        bucketVersion = 1;
    }

    bucket.count = 0;
    bucket.current = 0;
    bucket.distance = 0;
    memset(bucket.head, 0xFF, sizeof bucket.head);
}

static inline void pushBucket(uint32_t value) {
    uint8_t oldVersion = grid[value].bucketVersion;

    if (oldVersion != bucketVersion) {
        grid[value].bucketIndex = UINT8_MAX;
        grid[value].bucketNext = UINT32_MAX;
        grid[value].bucketVersion = bucketVersion;
    }

    if (oldVersion == bucketVersion) {
        uint8_t oldBucket = grid[value].bucketIndex;

        if (oldBucket != UINT8_MAX) {
            uint32_t *temp = &bucket.head[oldBucket];

            while (*temp != UINT32_MAX && *temp != value) {
                temp = &grid[*temp].bucketNext;
            }

            if (*temp == value) {
                *temp = grid[value].bucketNext;
                bucket.count--;
            }
        }
    }

    uint32_t newBucket = grid[value].distance % BUCKET_SIZE;
    grid[value].bucketNext = bucket.head[newBucket];
    bucket.head[newBucket] = value;
    grid[value].bucketIndex = newBucket;

    bucket.count++;
}

static inline uint32_t popBucket() {
    while (bucket.head[bucket.current] == UINT32_MAX) {
        bucket.current = (bucket.current + 1) % BUCKET_SIZE;
        bucket.distance++;
    }

    uint32_t poppedValue = bucket.head[bucket.current];
    bucket.head[bucket.current] = grid[poppedValue].bucketNext;
    grid[poppedValue].bucketNext = UINT32_MAX;
    grid[poppedValue].bucketIndex = UINT8_MAX;
    bucket.count--;

    return poppedValue;
}

static inline uint8_t isEmptyBucket() {
    return bucket.count == 0;
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

//changeCost support functions
void changeHexCost(Hex* hexagon, int8_t param, uint16_t radius);
static inline int floorDiv(int a, int b);

//toggleAirRoute support functions
static inline void activateAirRoute(uint32_t hex1Index, uint32_t hex2Index, int32_t sum);
static inline void removeAirRoute(uint32_t hex1Index, uint32_t hex2Index, uint8_t position);
static inline void swap(uint32_t *x1, uint32_t *x2);

int main() {
    uint8_t *geri = NULL;
    char buf[256];

    while (fgets(buf, sizeof buf, stdin)) {
        dispatchInput(buf);
    }

    free(grid);
    free(geri);
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

    initializeDeltaForAdjacents();

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
        newCost = getAirRouteCost(hexagon, i) + delta;

        if (newCost <= 0) {
            setAirRouteCost(hexagon, i, 1);
        } else if (newCost > 100) {
            setAirRouteCost(hexagon, i, 100);
        } else {
            setAirRouteCost(hexagon, i, newCost);
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
    for (uint8_t i = 0; i < grid[hex1Index].airRoutesNum; i++) {
        if (getAirRouteDestination(&grid[hex1Index], i) == hex2Index) {
            sum += getAirRouteCost(&grid[hex1Index], i);
            removeAirRoute(hex1Index, hex2Index, i);
            return;
        }
    }
    activateAirRoute(hex1Index, hex2Index, sum);
}

static inline void activateAirRoute(uint32_t hex1Index, uint32_t hex2Index, int32_t sum) {
    int32_t average;

    average = sum / (grid[hex1Index].airRoutesNum + 1);

    setAirRoute(&grid[hex1Index], grid[hex1Index].airRoutesNum, hex2Index, average);

    grid[hex1Index].airRoutesNum++;
}

static inline void removeAirRoute(uint32_t hex1Index, uint32_t hex2Index, uint8_t position) {
    for (int i = position; i < grid[hex1Index].airRoutesNum - 1; i++) {
        swap(&grid[hex1Index].airRoutes[i], &grid[hex1Index].airRoutes[i + 1]);
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

    initializeBucket();
    grid[hex1Index].distance = 0;
    grid[hex1Index].version = currentVersion;
    pushBucket(hex1Index);

    while (!isEmptyBucket()) {
        uint32_t hexIndex = popBucket();

        if (hexIndex == hex2Index) {
            break;
        }

        if (grid[hexIndex].landCost == 0) {
            continue;
        }

        uint32_t newDistance = grid[hexIndex].distance + grid[hexIndex].landCost;

        Offset hexCoord = linearToOffset(hexIndex);
        const int32_t *delta = (hexCoord.y & 1) ? dOdd : dEven;
        const int8_t *dx = (hexCoord.y & 1) ? dxOdd : dxEven;
        for (uint8_t i = 0; i < 6; i++) {
            int32_t adjIndex = hexIndex + delta[i];

            if ((uint32_t) adjIndex >= size) {
                continue;
            }

            if ((dx[i] == -1 && hexCoord.x == 0) || (dx[i] == 1 && hexCoord.x == columnsSize - 1)) {
                continue;
            }

            if (grid[adjIndex].version != currentVersion) {
                grid[adjIndex].distance = UINT32_MAX;
                grid[adjIndex].version = currentVersion;
            }

            if (newDistance < grid[adjIndex].distance) {
                grid[adjIndex].distance = newDistance;

                pushBucket(adjIndex);
            }
        }

        for (int i = 0; i < grid[hexIndex].airRoutesNum; i++) {
            newDistance = grid[hexIndex].distance + getAirRouteCost(&grid[hexIndex], i);

            uint32_t adjIndex = getAirRouteDestination(&grid[hexIndex], i);
            if (grid[adjIndex].version != currentVersion) {
                grid[adjIndex].distance = UINT32_MAX;
                grid[adjIndex].version = currentVersion;
            }

            if (newDistance < grid[adjIndex].distance) {
                grid[adjIndex].distance = newDistance;

                pushBucket(adjIndex);
            }
        }
    }
    
    if (grid[hex2Index].distance == UINT32_MAX || grid[hex2Index].version != currentVersion) {
        printf("-1\n");
    } else {
        printf("%u\n", grid[hex2Index].distance);
    }
}