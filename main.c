#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ячейка таблицы */
typedef struct {
    char *raw;      // текст из CSV
    int   value;    // вычисленное число
    int   state;    // 0=новая, 1=в обработке (DFS), 2=готово, для отслеживания рекурсий и повторов
    int   is_error;
} cell_t;

int find_col(cell_t **matrix, int cols, const char *name);
int find_row(cell_t **matrix, int rows, const char *name);
int parse_operand(cell_t **matrix, int rows, int cols, const char *s, int *pos, int *out_val);
int parse_formula(cell_t **matrix, int rows, int cols, const char *s, int *out_val);
int eval_cell(cell_t **matrix, int rows, int cols, int r, int c, int *out_val);

/* поиск имени столбца в нулевой строке */
int find_col(cell_t **matrix, int cols, const char *name) {
    for (int j = 1; j < cols; j++) {
        if (strcmp(matrix[0][j].raw, name) == 0) return j;
    }
    return -1;
}

/* поиск имени строки в нулевом столбце */
int find_row(cell_t **matrix, int rows, const char *name) {
    for (int i = 1; i < rows; i++) {
        if (strcmp(matrix[i][0].raw, name) == 0) return i;
    }
    return -1;
}

/* вычисление ячейки */
int eval_cell(cell_t **matrix, int rows, int cols, int r, int c, int *out_val) {
    cell_t *cell = &matrix[r][c];

    if (cell->state == 1) return -1;          // обнаружен цикл
    if (cell->state == 2) {                   // уже вычисляли
        if (cell->is_error) return -1;
        *out_val = cell->value;
        return 0;
    }

    cell->state = 1;                          // помечаем "в процессе"

    if (cell->raw[0] == '=') {                // это формула
        int val;
        if (parse_formula(matrix, rows, cols, cell->raw, &val)) {
            cell->is_error = 1;
            cell->state = 2;
            return -1;
        }
        cell->value = val;
    } else {                                   // должно быть число
        char *endptr = NULL;
        long v = strtol(cell->raw, &endptr, 10);
        if (endptr == cell->raw || *endptr != '\0' || v < 0) {
            cell->is_error = 1;
            cell->state = 2;
            return -1;
        }
        cell->value = (int)v;
    }

    cell->state = 2;
    *out_val = cell->value;
    return 0;
}

/* формат: =OP1 OPERATOR OP2 */
int parse_formula(cell_t **matrix, int rows, int cols, const char *s, int *out_val) {
    int pos = 1; // пропускаем '='
    int op1, op2;

    if (parse_operand(matrix, rows, cols, s, &pos, &op1)) return -1;

    while (s[pos] == ' ' || s[pos] == '\t') pos++;
    char op = s[pos++];
    if (op != '+' && op != '-' && op != '*' && op != '/') return -1;

    while (s[pos] == ' ' || s[pos] == '\t') pos++;
    if (parse_operand(matrix, rows, cols, s, &pos, &op2)) return -1;

    while (s[pos] == ' ' || s[pos] == '\t') pos++;
    if (s[pos] != '\0') return -1;

    switch (op) {
        case '+': *out_val = op1 + op2; break;
        case '-': *out_val = op1 - op2; break;
        case '*': *out_val = op1 * op2; break;
        case '/':
            if (op2 == 0) return -1;
            *out_val = op1 / op2;
            break;
    }
    return 0;
}

/* операнд: либо целое положительное число, либо ссылка ColumnRow */
int parse_operand(cell_t **matrix, int rows, int cols, const char *s, int *pos, int *out_val) {
    int i = *pos;
    while (s[i] == ' ' || s[i] == '\t') i++;

    // если число
    if (isdigit((unsigned char)s[i])) {
        long val = 0;
        while (isdigit((unsigned char)s[i])) {
            val = val * 10 + (s[i] - '0');
            i++;
        }
        *pos = i;
        *out_val = (int)val;
        return 0;
    }

    // если ссылка
    if (s[i] == '\0') return -1; // пустая строка

    char col_name[256];
    int col_len = 0;
    while (s[i] && !isdigit((unsigned char)s[i]) && s[i] != ' ' && s[i] != '\t') {
        col_name[col_len++] = s[i];
        i++;
        if (col_len >= 255) return -1;
    }
    col_name[col_len] = '\0';

    if (!isdigit((unsigned char)s[i])) return -1;

    char row_name[256];
    int row_len = 0;
    while (isdigit((unsigned char)s[i])) {
        row_name[row_len++] = s[i];
        i++;
        if (row_len >= 255) return -1;
    }
    row_name[row_len] = '\0';

    int c = find_col(matrix, cols, col_name);
    int r = find_row(matrix, rows, row_name);
    if (c < 0 || r < 0) return -1;

    if (eval_cell(matrix, rows, cols, r, c, out_val)) return -1; // рекурсивно вычисляем зависимую ячейку


    *pos = i;
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        const char msg[] = "Usage: csvreader file.csv\n";
        write(STDERR_FILENO, msg, sizeof(msg) - 1);
        return 1;
    }

    int fd = open(argv[1], O_RDONLY);
    if (fd < 0) return 1;

    int file_size = (int)lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);

    char *data = malloc(file_size + 1);
    if (!data) {
        close(fd);
        return 1;
    }

    read(fd, data, file_size);
    close(fd);
    data[file_size] = '\0';

    cell_t **matrix = NULL;
    int rows = 0, rows_cap = 0;
    int cols = 0;

    char *p = data;
    while (*p) {
        char *line_end = p;
        while (*line_end && *line_end != '\n') line_end++; // ищем конец строки (\n или \0)

        if (rows == 0) {
            int line_cols = 1;
            for (char *q = p; q < line_end; q++) {
                if (*q == ',') line_cols++; // считаем столбцы по запятым, пустая ячейка перед первой запятой тоже считается
            }
            cols = line_cols; // первая строка задаёт ширину таблицы
        } 

        if (rows == rows_cap) { // реаллоцируем
            rows_cap = rows_cap ? rows_cap * 2 : 16;
            matrix = realloc(matrix, rows_cap * sizeof(cell_t *));
        }
        matrix[rows] = malloc(cols * sizeof(cell_t));

        int c = 0;
        char *q = p;
        while (q < line_end && c < cols) {
            char *cell_start = q;
            while (q < line_end && *q != ',') q++;
            int len = q - cell_start;
            if (len > 0 && cell_start[len - 1] == '\r') len--;

            char *cell = malloc(len + 1);
            memcpy(cell, cell_start, len);
            cell[len] = '\0';

            matrix[rows][c].raw = cell;
            matrix[rows][c].value = 0;
            matrix[rows][c].state = 0;
            matrix[rows][c].is_error = 0;
            c++;

            if (q < line_end && *q == ',') q++; // пропускаем запятую-разделитель
        }
        // если в строке не хватило ячеек — дополняем пустыми
        while (c < cols) {
            char *cell = malloc(1);
            cell[0] = '\0';
            matrix[rows][c].raw = cell;
            matrix[rows][c].value = 0;
            matrix[rows][c].state = 0;
            matrix[rows][c].is_error = 0;
            c++;
        }

        rows++;
        p = line_end;
        if (*p == '\n') p++;
    }

    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            if (i == 0 || j == 0) {
                printf("%s", matrix[i][j].raw);
            } else {
                int val;
                if (eval_cell(matrix, rows, cols, i, j, &val)) {
                    printf("#ERROR");
                } else {
                    printf("%d", val);
                }
            }
            if (j + 1 < cols) printf(",");
        }
        printf("\n");
    }

    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) free(matrix[i][j].raw);
        free(matrix[i]);
    }
    free(matrix);
    free(data);

    fflush(stdout);
    return 0;
}