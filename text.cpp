/*
 * Лабораторная работа №1
 * Тема: Оптимизация обмена
 * Вариант: текстовый файл >= 100 КБ, чтение в ОЗУ построчно,
 *          20 повторов, характеристика — максимальный размер строки 64..128 байт.
 *
 * Win32, чистый C.
 * Динамическая память через VirtualAlloc/VirtualFree.
 * Копирование данных в буфер ОЗУ через CopyMemory.
 */

#define _CRT_SECURE_NO_WARNINGS

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MIN_LINE_SIZE       64
#define MAX_LINE_SIZE       128
#define REPETITIONS         20
#define FILE_SIZE_KB        100
#define READ_BUFFER_SIZE    (MAX_LINE_SIZE + 3)
#define OUTPUT_BUFFER_SIZE  (1024 * 1024)

static void print_win_error(const char *operation)
{
    DWORD error_code = GetLastError();
    char message[512];
    DWORD len;

    len = FormatMessageA(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        error_code,
        0,
        message,
        sizeof(message),
        NULL
    );

    if (len == 0) {
        printf("Ошибка: %s (код %lu)\n", operation, (unsigned long)error_code);
    } else {
        printf("Ошибка: %s: %s", operation, message);
    }
}

static int generate_test_file(const char *filename, int max_line_length)
{
    HANDLE hFile = INVALID_HANDLE_VALUE;
    DWORD written;
    char line[MAX_LINE_SIZE + 2];
    unsigned long long total_written = 0;
    unsigned int state = 123456789u;

    hFile = CreateFileA(
        filename,
        GENERIC_WRITE,
        0,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hFile == INVALID_HANDLE_VALUE) {
        print_win_error("CreateFileA при создании тестового файла");
        return 0;
    }

    while (total_written < (unsigned long long)FILE_SIZE_KB * 1024ULL) {
        int length;
        int i;

        /* Псевдослучайная длина от 1 до max_line_length. */
        state = state * 1664525u + 1013904223u;
        length = 1 + (int)(state % (unsigned int)max_line_length);

        /*
         * Заполняем строку печатными ASCII-символами.
         * Последовательность генерируется детерминированно.
         */
        for (i = 0; i < length; ++i) {
            state = state * 1664525u + 1013904223u;
            line[i] = (char)('A' + (state % 26u));
        }
        line[length] = '\r';
        line[length + 1] = '\n';

        if (!WriteFile(hFile, line, (DWORD)(length + 2), &written, NULL)) {
            print_win_error("WriteFile");
            CloseHandle(hFile);
            return 0;
        }

        if (written != (DWORD)(length + 2)) {
            printf("Ошибка: WriteFile записал не все данные.\n");
            CloseHandle(hFile);
            return 0;
        }

        total_written += written;
    }

    if (!CloseHandle(hFile)) {
        print_win_error("CloseHandle после генерации файла");
        return 0;
    }

    return 1;
}

static int read_file_line_by_line(const char *filename,
                                  unsigned char *memory_buffer,
                                  SIZE_T memory_capacity,
                                  unsigned long long *bytes_read)
{
    FILE *file = NULL;
    char line[READ_BUFFER_SIZE];
    SIZE_T position = 0;
    unsigned long long total = 0;

    if (memory_buffer == NULL || bytes_read == NULL) {
        printf("Ошибка: передан NULL-указатель.\n");
        return 0;
    }

    file = fopen(filename, "rb");
    if (file == NULL) {
        printf("Ошибка: не удалось открыть файл \"%s\".\n", filename);
        return 0;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        SIZE_T length = strlen(line);

        if (position + length > memory_capacity) {
            printf("Ошибка: выделенного буфера ОЗУ недостаточно.\n");
            fclose(file);
            return 0;
        }

        /*
         * Копируем считанную строку в буфер оперативной памяти.
         * CopyMemory — Win32-макрос для копирования блока памяти.
         */
        CopyMemory(memory_buffer + position, line, length);

        position += length;
        total += (unsigned long long)length;
    }

    if (ferror(file)) {
        printf("Ошибка: ошибка чтения файла.\n");
        fclose(file);
        return 0;
    }

    if (fclose(file) != 0) {
        printf("Ошибка: не удалось закрыть файл после чтения.\n");
        return 0;
    }

    *bytes_read = total;
    return 1;
}

static double elapsed_seconds(LARGE_INTEGER start, LARGE_INTEGER finish,
                              LARGE_INTEGER frequency)
{
    return (double)(finish.QuadPart - start.QuadPart) /
           (double)frequency.QuadPart;
}

int main(void)
{
    const char *filename = "test_data.txt";
    const char *csv_filename = "results.csv";
    const char *summary_filename = "summary.csv";

    LARGE_INTEGER frequency;
    LARGE_INTEGER start;
    LARGE_INTEGER finish;

    unsigned char *memory_buffer = NULL;
    SIZE_T buffer_capacity = 0;

    FILE *csv = NULL;
    FILE *summary = NULL;
    int max_line_length;
    int repetition;

    if (!QueryPerformanceFrequency(&frequency)) {
        print_win_error("QueryPerformanceFrequency");
        return EXIT_FAILURE;
    }

    /*
     * Буфер ОЗУ берём с запасом. Файл имеет минимум 100 КБ,
     * а размер файла заранее неизвестен точно.
     * 2 МБ достаточно для лабораторного тестового файла.
     */
    buffer_capacity = 2ULL * 1024ULL * 1024ULL;

    memory_buffer = (unsigned char *)VirtualAlloc(
        NULL,
        buffer_capacity,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_READWRITE
    );

    if (memory_buffer == NULL) {
        print_win_error("VirtualAlloc");
        return EXIT_FAILURE;
    }

    csv = fopen(csv_filename, "w");
    if (csv == NULL) {
        printf("Ошибка: не удалось создать \"%s\".\n", csv_filename);
        if (!VirtualFree(memory_buffer, 0, MEM_RELEASE)) {
            print_win_error("VirtualFree");
        }
        return EXIT_FAILURE;
    }

    summary = fopen(summary_filename, "w");
    if (summary == NULL) {
        printf("Ошибка: не удалось создать \"%s\".\n", summary_filename);
        fclose(csv);
        if (!VirtualFree(memory_buffer, 0, MEM_RELEASE)) {
            print_win_error("VirtualFree");
        }
        return EXIT_FAILURE;
    }

    fprintf(csv, "max_line_size,repeat,file_bytes,time_sec,speed_kb_s\n");
    fprintf(summary, "max_line_size,avg_time_sec,avg_speed_kb_s\n");

    printf("Лабораторная работа №1. Оптимизация обмена\n");
    printf("Чтение текстового файла построчно в ОЗУ.\n");
    printf("Диапазон максимальной длины строки: %d..%d байт.\n",
           MIN_LINE_SIZE, MAX_LINE_SIZE);
    printf("Количество повторений: %d.\n\n", REPETITIONS);

    for (max_line_length = MIN_LINE_SIZE;
         max_line_length <= MAX_LINE_SIZE;
         ++max_line_length)
    {
        double total_time = 0.0;
        unsigned long long measured_bytes = 0;

        printf("Максимальный размер строки: %d байт\n",
               max_line_length);

        /*
         * Для каждого значения характеристики создаётся новый
         * случайный тестовый файл.
         */
        if (!generate_test_file(filename, max_line_length)) {
            fclose(csv);
            if (!VirtualFree(memory_buffer, 0, MEM_RELEASE)) {
                print_win_error("VirtualFree");
            }
            return EXIT_FAILURE;
        }

        for (repetition = 1; repetition <= REPETITIONS; ++repetition) {
            unsigned long long bytes_read = 0;
            double seconds;
            double speed_kb_s;

            if (!QueryPerformanceCounter(&start)) {
                print_win_error("QueryPerformanceCounter");
                fclose(csv);
                if (!VirtualFree(memory_buffer, 0, MEM_RELEASE)) {
                    print_win_error("VirtualFree");
                }
                return EXIT_FAILURE;
            }

            if (!read_file_line_by_line(
                    filename,
                    memory_buffer,
                    buffer_capacity,
                    &bytes_read)) {
                fclose(csv);
                if (!VirtualFree(memory_buffer, 0, MEM_RELEASE)) {
                    print_win_error("VirtualFree");
                }
                return EXIT_FAILURE;
            }

            if (!QueryPerformanceCounter(&finish)) {
                print_win_error("QueryPerformanceCounter");
                fclose(csv);
                if (!VirtualFree(memory_buffer, 0, MEM_RELEASE)) {
                    print_win_error("VirtualFree");
                }
                return EXIT_FAILURE;
            }

            seconds = elapsed_seconds(start, finish, frequency);

            if (seconds <= 0.0) {
                printf("Ошибка: некорректное время измерения.\n");
                fclose(csv);
                if (!VirtualFree(memory_buffer, 0, MEM_RELEASE)) {
                    print_win_error("VirtualFree");
                }
                return EXIT_FAILURE;
            }

            speed_kb_s = ((double)bytes_read / 1024.0) / seconds;

            total_time += seconds;
            measured_bytes = bytes_read;

            fprintf(csv, "%d,%d,%llu,%.9f,%.3f\n",
                    max_line_length,
                    repetition,
                    bytes_read,
                    seconds,
                    speed_kb_s);
        }

        printf("  Размер файла: %llu байт\n", measured_bytes);
        printf("  Среднее время: %.6f с\n", total_time / REPETITIONS);
        printf("  Средняя скорость: %.3f Кбайт/с\n\n",
               ((double)measured_bytes / 1024.0) /
               (total_time / REPETITIONS));

        fprintf(summary, "%d,%.9f,%.3f\n",
                max_line_length,
                total_time / REPETITIONS,
                ((double)measured_bytes / 1024.0) /
                (total_time / REPETITIONS));
    }

    if (fclose(csv) != 0) {
        printf("Ошибка: не удалось закрыть CSV-файл.\n");
        if (!VirtualFree(memory_buffer, 0, MEM_RELEASE)) {
            print_win_error("VirtualFree");
        }
        return EXIT_FAILURE;
    }

    if (!VirtualFree(memory_buffer, 0, MEM_RELEASE)) {
        print_win_error("VirtualFree");
        return EXIT_FAILURE;
    }

    printf("Готово.\n");
    printf("Созданы файлы:\n");
    printf("  %s — тестовые данные\n", filename);
    printf("  %s — результаты 1300 измерений\n", csv_filename);\n    printf("  %s — средние значения для графика\n", summary_filename);
    printf("\nДля графика используйте среднюю скорость для каждого\n");
    printf("значения max_line_size (64..128).\n");

    return EXIT_SUCCESS;
}
