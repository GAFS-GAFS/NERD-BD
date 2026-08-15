#ifndef MINIQUERY_PIPELINE_H
#define MINIQUERY_PIPELINE_H

#include <stdbool.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include "table.h"

// Alias para Tupla, conectando com a estrutura existente
typedef EmployeeRow Tuple;

// --- Operador 1: Sequential Scan ---
typedef struct {
    const Table *table;
    size_t current_index;
} SeqScanState;

void seq_scan_init(SeqScanState *state, const Table *table);
bool seq_scan_next(SeqScanState *state, Tuple *t);

// --- Operador 2: Filtro ---
typedef struct {
    SeqScanState *child_scan;
    double min_salary;
    
    // Variaveis para as metricas obrigatorias
    size_t rows_read;
    size_t rows_passed;
} FilterState;

void filter_init(FilterState *state, SeqScanState *child, double min_salary);
bool filter_next(FilterState *state, Tuple *t);

// Função principal que o main.c vai chamar para rodar o modelo do Grupo B
void execute_pipeline_query(const Table *table);

#endif