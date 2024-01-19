// Require for thread affinity
#define _GNU_SOURCE
// Headers
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <semaphore.h>
#include <signal.h>
#include <sched.h>

// Definición de colores
const char *c_red = "\033[31m";
const char *c_green = "\033[32m";
const char *c_blue = "\033[34m";
const char *c_magenta = "\033[35m";
const char *c_white = "\033[37m";
const char *c_yellow = "\033[33m";
const char *c_cian = "\033[36m";
const char *c_end = "\033[0m";
const char *c_orange = "\033[0;33m";

// Definiciones de constantes
const float H1_CAPACITY = 15.0;
const float H2_CAPACITY = 5.0;
const float H3_CAPACITY = 2.0;
const float MIN_GENERATION = 100.0;
const float MAX_GENERATION = 150.0;
const float NO_RAIN_INCREMENT = 0.0;
const float AGUACERO_INCREMENT = 2.0; // Incremento para Aguacero
const float DILUVIO_INCREMENT = 4.0;  // Incremento para Diluvio
const int NO_RAIN_DURATION = 0;
const int AGUACERO_DURATION = 10; // Duración de Aguacero
const int DILUVIO_DURATION = 5;   // Duración de Diluvio
volatile sig_atomic_t shutdownRequested = 0;

// Estructura para las centrales hidroeléctricas
typedef struct
{
    char *name;
    float capacity;
    float minWaterLevel;
    float maxWaterLevel;
    float waterLevel;
    int isActive;
    pthread_t thread;
} HydroelectricPlant;

typedef struct HydroelectricPlantNode
{
    HydroelectricPlant *plant;
    struct HydroelectricPlantNode *next;
} HydroelectricPlantNode;

// Variables globales
HydroelectricPlantNode *head = NULL;
pthread_mutex_t listMutex, energyMutex;
sem_t adjustmentSemaphore, sortingSemaphore;
float probA, probB, probC;
float totalEnergyGenerated = 0.0;
int lastShots = 4;
bool waitingForRecover = false;

// Prototipos de funciones
void createAndInsertPlants(int numPlants, const char *plantType, float capacity, float minWaterLevel, float maxWaterLevel);
void *hydroelectricPlantRoutine(void *arg);
void *sortingThreadRoutine();
bool applyGreedyAlgorithm();
void insertSorted(HydroelectricPlant *plant);
int comparePlants(HydroelectricPlant *a, HydroelectricPlant *b);
void sortList();
void activatePlant(HydroelectricPlant *plant);
void deactivatePlant(HydroelectricPlant *plant);
void shutdownPlantsAndPrintFinalStatus();

void signalHandler(int sig)
{
    if (sig == SIGINT)
    {
        shutdownRequested = 1;
        printf("%s\nCerrando programa senal de cierre recibida***\n%s", c_red, c_end);
    }
}
int main(int argc, char *argv[])
{
    if (argc != 7)
    {
        fprintf(stderr, "Uso: %s <Prob A> <Prob B> <Prob C> <Num H1> <Num H2> <Num H3>\n", argv[0]);
        return 1;
    }

    probA = atof(argv[1]);
    probB = atof(argv[2]);
    probC = atof(argv[3]);
    int numH1 = atoi(argv[4]);
    int numH2 = atoi(argv[5]);
    int numH3 = atoi(argv[6]);

    if (probA + probB + probC != 1.0f)
    {
        fprintf(stderr, "Error: la suma de las probabilidades debe ser 1.\n");
        return 1;
    }
    // Calcula la capacidad total máxima
    float totalMaxCapacity = numH1 * H1_CAPACITY + numH2 * H2_CAPACITY + numH3 * H3_CAPACITY;

    // Verifica si la capacidad total es suficiente
    if (totalMaxCapacity < MIN_GENERATION)
    {
        fprintf(stderr, "Error: La capacidad total máxima de %f MW/s no alcanza el mínimo requerido de %f MW/s.\n", totalMaxCapacity, MIN_GENERATION);
        return 1;
    }
    // Configuración del manejador de señales
    struct sigaction sa;
    sa.sa_handler = &signalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);

    // Inicializar mutex y semáforos
    // pthread_mutex_init(&listMutex, NULL);
    pthread_mutex_init(&energyMutex, NULL);
    sem_init(&adjustmentSemaphore, 0, 0);
    sem_init(&sortingSemaphore, 0, 0);

    // Crear y añadir centrales a la lista
    createAndInsertPlants(numH1, "H1", H1_CAPACITY, 50.0, 200.0);
    createAndInsertPlants(numH2, "H2", H2_CAPACITY, 25.0, 100.0);
    createAndInsertPlants(numH3, "H3", H3_CAPACITY, 10.0, 50.0);

    // Greedy para definir que centrales van a estar activas antes de crear los hilos
    applyGreedyAlgorithm();

    // Crear y lanzar hilos de centrales con afinidad
    int num_cores = sysconf(_SC_NPROCESSORS_ONLN); // Obtén el número de núcleos de CPU
    HydroelectricPlantNode *current = head;
    int core_id = 0;
    while (current)
    {
        pthread_attr_t attr;
        cpu_set_t cpus;
        pthread_attr_init(&attr);
        CPU_ZERO(&cpus);
        CPU_SET(core_id % num_cores, &cpus); // Asigna el hilo a un núcleo específico
        pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpus);
        pthread_create(&current->plant->thread, &attr, hydroelectricPlantRoutine, current->plant);
        pthread_attr_destroy(&attr); // Limpia los atributos del hilo después de su uso
        core_id++;
        current = current->next;
    }

    // Crear y lanzar el hilo de ordenamiento
    pthread_t sortingThread;
    pthread_create(&sortingThread, NULL, sortingThreadRoutine, NULL);

    // Bucle principal para Greedy y semaforo para Sorthing thread
    while (!shutdownRequested)
    {
        sem_wait(&adjustmentSemaphore);
        printf("%sCapacidad ajuste requerido: %f MW/s%s\n", c_yellow, totalEnergyGenerated, c_end);
        applyGreedyAlgorithm();

        printf("%sCapacidad ajustada: %f MW/s%s\n", c_green, totalEnergyGenerated, c_end);
        // no se lo puede mover antes que el greesy porque a lo que uno trata de recorrer la lista el otro la mueve
        // y si se queda en un puntero que el sorting lo vuelve a poner primero le toca recorrer toda la lista de nuevo
        sem_post(&sortingSemaphore);
    }

    // Esperar a que todos los hilos finalicen
    current = head;
    while (current)
    {
        pthread_join(current->plant->thread, NULL);
        current = current->next;
    }
    pthread_join(sortingThread, NULL);

    // Liberar recursos
    sem_destroy(&adjustmentSemaphore);
    sem_destroy(&sortingSemaphore);

    current = head;
    while (current)
    {
        free(current->plant);
        HydroelectricPlantNode *temp = current;
        current = current->next;
        free(temp);
    }
    return 0;
}
void createAndInsertPlants(int numPlants, const char *plantType, float capacity, float minWaterLevel, float maxWaterLevel)
{
    for (int i = 0; i < numPlants; ++i)
    {
        HydroelectricPlant *plant = malloc(sizeof(HydroelectricPlant));
        if (plant == NULL)
        {
            fprintf(stderr, "Error: NO se pudo reservar memoria para las centrales.\n");
            exit(-1); // Cambiado a exit para salir del programa completamente
        }
        char nameBuffer[15];
        sprintf(nameBuffer, "ID_%d_%s", i, plantType);
        plant->name = strdup(nameBuffer);
        plant->capacity = capacity;
        plant->minWaterLevel = minWaterLevel;
        plant->maxWaterLevel = maxWaterLevel;
        plant->waterLevel = (minWaterLevel + maxWaterLevel) / 2;
        plant->isActive = 0;
        insertSorted(plant);
    }
}

void *hydroelectricPlantRoutine(void *arg)
{
    HydroelectricPlant *plant = (HydroelectricPlant *)arg;
    int rainDuration = 0;
    float rainIncrement = 0.0;
    char *rain_type = "NL";
    while (!shutdownRequested)
    {
        if (rainDuration > 0)
        {
            plant->waterLevel += rainIncrement;
            rainDuration--;
        }
        else
        {
            // Simular un nuevo evento de lluvia
            float prob = (float)rand() / RAND_MAX;
            if (prob < probA)
            {
                rainIncrement = NO_RAIN_INCREMENT;
                rainDuration = NO_RAIN_DURATION;
                rain_type = "NL";
            }
            else if (prob < probA + probB)
            {
                rainIncrement = AGUACERO_INCREMENT;
                rainDuration = AGUACERO_DURATION;
                rain_type = "AG";
            }
            else
            {
                rainIncrement = DILUVIO_INCREMENT;
                rainDuration = DILUVIO_DURATION;
                rain_type = "DI";
            }
        }
        // Simulación de la operación de la central
        if (plant->isActive && !waitingForRecover)
        {
            // Reducir el nivel del agua debido a la generación de energía
            plant->waterLevel -= 5.0;
            float water_flow = rainIncrement - 5;
            printf("%s %s %s Central %s - water_level: %.2f - water_flow: %.2f m/s.\n", c_cian, rain_type, c_end, plant->name, plant->waterLevel, water_flow);

            // Desactivar la central si el nivel de agua está fuera de los límites
            if (plant->waterLevel <= plant->minWaterLevel || plant->waterLevel >= plant->maxWaterLevel)
            {
                deactivatePlant(plant); // Esto también podría manejar la lógica del semáforo
                sem_post(&adjustmentSemaphore);
                printf("Desactivando central tipo %s\n", plant->name);
            }
        }
        if (!plant->isActive && plant->waterLevel > plant->maxWaterLevel)
        {
            plant->waterLevel -= 5.0;
        }
        sleep(1); // Esperar un segundo antes de la próxima iteración
    }
    // Limpieza y salida ordenada del hilo
    pthread_exit(NULL);
    return NULL;
}

void *sortingThreadRoutine()
{
    while (!shutdownRequested)
    {
        printf("%sExcecuting sorting thread.%s\n", c_magenta, c_end);
        sortList();
        // Esperar una señal para comenzar el ordenamiento
        sem_wait(&sortingSemaphore);
    }
    // Limpieza y salida ordenada del hilo
    pthread_exit(NULL);
    return NULL;
}

void deactivatePlant(HydroelectricPlant *plant)
{
    plant->isActive = 0;
    pthread_mutex_lock(&energyMutex);
    totalEnergyGenerated -= plant->capacity;
    pthread_mutex_unlock(&energyMutex);
}
void activatePlant(HydroelectricPlant *plant)
{
    plant->isActive = 1;
    pthread_mutex_lock(&energyMutex);
    totalEnergyGenerated += plant->capacity;
    pthread_mutex_unlock(&energyMutex);
}

void insertSorted(HydroelectricPlant *plant)
{
    HydroelectricPlantNode *newNode = malloc(sizeof(HydroelectricPlantNode));
    newNode->plant = plant;
    newNode->next = NULL;

    if (head == NULL || comparePlants(newNode->plant, head->plant) > 0)
    {
        newNode->next = head;
        head = newNode;
    }
    else
    {
        HydroelectricPlantNode *current = head;
        while (current->next != NULL && comparePlants(newNode->plant, current->next->plant) <= 0)
        {
            current = current->next;
        }
        newNode->next = current->next;
        current->next = newNode;
    }
}
// prioridad orden de la lista
int comparePlants(HydroelectricPlant *a, HydroelectricPlant *b)
{
    // Primero comparar por capacidad
    if (a->capacity != b->capacity)
    {
        return (a->capacity > b->capacity) ? 1 : -1;
    }
    // Luego por nivel de agua
    if (a->waterLevel != b->waterLevel)
    {
        float relative_level_a = (a->waterLevel - a->minWaterLevel) / (a->maxWaterLevel - a->minWaterLevel);
        float relative_level_b = (b->waterLevel - b->minWaterLevel) / (b->maxWaterLevel - b->minWaterLevel);
        return (relative_level_a > relative_level_b) ? 1 : -1;
    }
    return 0;
}

void sortList()
{
    if (!head || !head->next)
    {
        return; // No hay nada que ordenar si la lista está vacía o tiene un solo elemento
    }

    HydroelectricPlantNode *sorted = NULL;  // Lista ordenada
    HydroelectricPlantNode *current = head; // Apuntador al elemento actual en la lista original

    while (current != NULL)
    {
        HydroelectricPlantNode *next = current->next; // Guardar el siguiente elemento

        // Insertar el elemento actual en la posición correcta en la lista ordenada
        if (sorted == NULL || comparePlants(current->plant, sorted->plant) > 0)
        {
            current->next = sorted;
            sorted = current;
        }
        else
        {
            HydroelectricPlantNode *sortedCurrent = sorted;
            while (sortedCurrent->next != NULL && comparePlants(current->plant, sortedCurrent->next->plant) <= 0)
            {
                sortedCurrent = sortedCurrent->next;
            }
            current->next = sortedCurrent->next;
            sortedCurrent->next = current;
        }

        current = next; // Mover al siguiente elemento en la lista original
    }

    head = sorted; // Actualizar el inicio de la lista con la lista ordenada
}

bool applyGreedyAlgorithm()
{
    printf("Applying greedy algorithm.\n");

    float currentGeneration = totalEnergyGenerated;
    HydroelectricPlantNode *currentNode = head;
    currentNode = head;

    // Activar centrales de manera óptima
    while (currentNode != NULL)
    {
        if (!currentNode->plant->isActive && currentNode->plant->waterLevel > currentNode->plant->minWaterLevel &&
            currentGeneration + currentNode->plant->capacity <= MAX_GENERATION)
        {
            activatePlant(currentNode->plant);
            printf("%sActivada Central %s %s\n", c_blue, currentNode->plant->name, c_end);
            currentGeneration += currentNode->plant->capacity;
        }
        if (currentGeneration >= MIN_GENERATION)
        {
            waitingForRecover = false;
            return true; // Detenerse si se alcanza la generación mínima
        }
        currentNode = currentNode->next;
    }
    if (lastShots > 1)
    {
        waitingForRecover = true;
        lastShots -= 1;
        printf("%sALERTA: comenzando intentos de recuperacion antes de cierre inmediato - %i.%s\n", c_red, lastShots, c_end);
        sleep(1);
        return applyGreedyAlgorithm();
    }
    else
    {
        shutdownPlantsAndPrintFinalStatus();
    }
    return false;
}

void shutdownPlantsAndPrintFinalStatus()
{
    printf("%sNo hay ninguna combinacion que pueda ayudar a mantener la planta encendida.Procedemos a desactivar todo.%s\n", c_red, c_end);
    printf("%sA continuacion puede visualizar el ultimo estado de las centrales.%s\n", c_red, c_end);
    HydroelectricPlantNode *current = head;

    while (current != NULL)
    {
        HydroelectricPlant *plant = current->plant;
        printf("Central: %s, Min: %.2f, Max: %.2f, Actual: %.2f, Estado: %s\n",
               plant->name,
               plant->minWaterLevel,
               plant->maxWaterLevel,
               plant->waterLevel,
               plant->isActive ? "Activada" : "Desactivada");
        current = current->next;
    }
    shutdownRequested = 1;
}