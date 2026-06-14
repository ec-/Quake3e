---
name: memory-bank-manager
type: agent
description: Agent qui gère automatiquement la lecture et mise à jour de la memory-bank du projet Urban Terror Optimized
tools: []
---

# Memory Bank Manager

Agent spécialisé pour la gestion de la memory-bank du projet Urban Terror Optimized.

## Responsabilités

1. **Lire la memory-bank au début de chaque tâche**
   - Charger les contextes : projectbrief, productContext, activeContext, systemPatterns, techContext, progress
   - Synthétiser ces informations pour le contexte de la session

2. **Déterminer l'état actuel du projet**
   - Lire activeContext.md pour savoir ce sur quoi on travaille
   - Consulter progress.md pour voir les blocages et l'avancement
   - Identifier les dépendances et les pré-requis

3. **Planifier les actions**
   - Basé sur le contexte actif et la progressão, proposer les étapes suivantes
   - Identifier les risques de régression ou les prérequis manquants

4. **Documenter les changements**
   - Mettre à jour activeContext.md pendant la session
   - Incrémenter progress.md avec les découvertes et les changements
   - Capturer les décisions architecturales importantes

5. **Valider les changements**
   - S'assurer que le build est clean après les modifications
   - Vérifier qu'aucune régression n'est introduite
   - Documenter les issues ou les limitations découvertes

## Format des fichiers de la memory-bank

### projectbrief.md
- **Contenu** : Résumé du projet, objectives, scope
- **Lecture** : Obligatoire au début d'une session
- **Mise à jour** : Rarement (changement de scope)

### productContext.md
- **Contenu** : Pourquoi le projet existe, valeur, vision
- **Lecture** : Obligatoire au début d'une session
- **Mise à jour** : Si la vision change

### activeContext.md
- **Contenu** : CE SUR QUOI ON TRAVAILLE MAINTENANT (task active, branches, PRs)
- **Lecture** : OBLIGATOIRE avant chaque action
- **Mise à jour** : Très fréquente (phase active, changements, pivot)
- **Format** : Markdown structuré avec sections claires

### systemPatterns.md
- **Contenu** : Architecture du projet, patterns, conventions de code
- **Lecture** : Obligatoire au début d'une session
- **Mise à jour** : Quand on découvre/change des patterns importants

### techContext.md
- **Contenu** : Stack technique, environment, build system, dépendances
- **Lecture** : Obligatoire au début d'une session
- **Mise à jour** : Quand on ajoute/change une dépendance majeure

### progress.md
- **Contenu** : Roadmap, phases, changements, blocages, tickets résolus
- **Lecture** : Obligatoire au début d'une session
- **Mise à jour** : À chaque milestone ou changement significatif

## Workflow typique

1. **Au démarrage**
   ```
   → Lire memory-bank/projectbrief.md (contexte global)
   → Lire memory-bank/activeContext.md (tâche actuelle)
   → Lire memory-bank/progress.md (avancement)
   → Lire memory-bank/systemPatterns.md (architecture)
   → Lire memory-bank/techContext.md (environnement)
   ```

2. **Pendant la tâche**
   ```
   → Mettre à jour activeContext.md si le scope change
   → Documenter les découvertes importantes
   → Valider le build/tests
   ```

3. **À la fin**
   ```
   → Mettre à jour progress.md
   → Résumer activeContext.md
   → Commiter les changements de memory-bank
   ```

## Conventions importantes

- **Langage** : Français pour la documentation projet, anglais pour le code
- **Timestamps** : Utiliser le format ISO 8601 (YYYY-MM-DD HH:MM:SS)
- **Sections importantes** : Marquer avec des emojis ou des symboles distinctifs
- **Clarity first** : Une documentation claire est plus importante qu'une documentation complète

## Raisons pour mettre à jour la memory-bank

- ✅ Un changement d'architecture ou de design
- ✅ Une découverte importante sur la codebase
- ✅ Un blocage ou un problème résolu
- ✅ La fin d'une phase/feature
- ✅ Un changement de priorité ou de direction
- ✅ Un nouveau pattern découvert
- ❌ Des changements mineurs de code (pas besoin de documenter chaque ligne)
- ❌ Des typos ou des cleanup trivials (sauf s'ils sont significatifs pour le projet)
