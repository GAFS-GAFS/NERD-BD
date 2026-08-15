#include "pipeline.h"

// Inicializa o estado do Scan
void seq_scan_init(SeqScanState *state, const Table *table) {
    state->table = table;
    state->current_index = 0;
}

// Retorna a próxima tupla da tabela (Tuple-at-a-time)
bool seq_scan_next(SeqScanState *state, Tuple *t) {
    // Acessa o tamanho da tabela conforme definido no table.c
    if (state->current_index < state->table->size) {
        *t = state->table->rows[state->current_index];
        state->current_index++;
        return true;
    }
    return false; // Fim da tabela
}

// Inicializa o estado do Filtro
void filter_init(FilterState *state, SeqScanState *child, double min_salary) {
    state->child_scan = child;
    state->min_salary = min_salary;
    state->rows_read = 0;
    state->rows_passed = 0;
}

// Pede linhas ao Scan até achar uma que passe na condição (salario > 5000)
bool filter_next(FilterState *state, Tuple *t) {
    while (seq_scan_next(state->child_scan, t)) {
        state->rows_read++;
        
        if (t->salary > state->min_salary) {
            state->rows_passed++;
            return true;
        }
    }
    return false; // Nenhuma linha restante atende ao filtro
}

void execute_pipeline_query(const Table *table) {
    SeqScanState scan;
    FilterState filter;
    
    // Inicializa o Pipeline
    seq_scan_init(&scan, table);
    filter_init(&filter, &scan, 5000.0); // Filtro: salario > 5000

    size_t agg_count = 0;
    double agg_sum_salary = 0.0;
    Tuple current_tuple;

    printf("\n--- Executando Modelo: Pipeline (Tuple-at-a-time) ---\n");

    clock_t start_time = clock();

    // Loop do iterador: puxa uma linha por vez
    while (filter_next(&filter, &current_tuple)) {
        agg_count++;
        agg_sum_salary += current_tuple.salary;
    }

    clock_t end_time = clock();
    double time_spent_ms = (double)(end_time - start_time) / CLOCKS_PER_SEC * 1000.0;

    double avg_salary = 0.0;
    if (agg_count > 0) {
        avg_salary = agg_sum_salary / agg_count;
    }

    // Prints dos resultados exigidos
    printf("Resultados:\n");
    printf("  COUNT(*): %zu\n", agg_count);
    printf("  AVG(salario): %.2f\n\n", avg_salary);

    printf("Metricas:\n");
    printf("  Linhas lidas: %zu\n", filter.rows_read);
    printf("  Linhas aprovadas: %zu\n", filter.rows_passed);
    printf("  Tempo total: %.2f ms\n", time_spent_ms);
}