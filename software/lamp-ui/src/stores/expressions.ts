/**
 * Expressions Store
 *
 * Manages the expression library - reading expression definitions
 * and providing getters for expression data.
 */

import { defineStore } from 'pinia'
import { ref, computed } from 'vue'
import type { FieldDefinition } from '@/types'
import expressionsLibrary from '@/lib/expressions.json'

export interface ExpressionDefinition {
  index: string
  name: string
  description: string
  fields: FieldDefinition[]
}

export const useExpressionsStore = defineStore('expressions', () => {
  // State - load expressions from library
  const expressions = ref<ExpressionDefinition[]>(expressionsLibrary as ExpressionDefinition[])

  // Getters
  const expressionsByIndex = computed(() => {
    const map = new Map<string, ExpressionDefinition>()
    expressions.value.forEach((expr) => {
      map.set(expr.index, expr)
    })
    return map
  })

  const expressionsList = computed(() => {
    return [...expressions.value].sort((a, b) => a.name.localeCompare(b.name))
  })

  // Methods
  const getExpression = (index: string): ExpressionDefinition | undefined => {
    return expressionsByIndex.value.get(index)
  }

  const getExpressionName = (index: string): string => {
    return expressionsByIndex.value.get(index)?.name || index
  }

  const getExpressionDescription = (index: string): string => {
    return expressionsByIndex.value.get(index)?.description || ''
  }

  const getExpressionFields = (index: string): FieldDefinition[] => {
    return expressionsByIndex.value.get(index)?.fields || []
  }

  const getAllExpressionIndices = (): string[] => {
    return expressions.value.map((expr) => expr.index)
  }

  return {
    // State
    expressions,

    // Computed
    expressionsByIndex,
    expressionsList,

    // Methods
    getExpression,
    getExpressionName,
    getExpressionDescription,
    getExpressionFields,
    getAllExpressionIndices,
  }
})

