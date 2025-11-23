import { globalIgnores } from 'eslint/config'
import { defineConfigWithVueTs, vueTsConfigs } from '@vue/eslint-config-typescript'
import pluginVue from 'eslint-plugin-vue'
import pluginVitest from '@vitest/eslint-plugin'
import skipFormatting from '@vue/eslint-config-prettier/skip-formatting'

export default defineConfigWithVueTs(
  globalIgnores(['**/dist/**', '**/dist-ssr/**', '**/coverage/**', "**/__tests__/**"]),
  pluginVue.configs['flat/essential'],
  vueTsConfigs.recommended,
  skipFormatting,
  {
    ...pluginVitest.configs.recommended,
    name: 'app/files-to-lint',
    files: ['**/*.{js,ts,mts,tsx,vue}'],
    languageOptions: {
      parserOptions: {
        tsconfigRootDir: __dirname
      }
    }
  },
)
