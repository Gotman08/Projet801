# Schémas pour le rapport

Ce dossier contient les **prompts ChatGPT** (DALL-E / image generation) pour
produire les schémas illustratifs du rapport. Chaque schéma a son propre
fichier `.md` avec :

- contexte et placement dans le rapport
- prompt à coller dans ChatGPT
- résolution / format souhaités
- contraintes (peu de texte, schéma technique, palette neutre)

**Convention de nommage** :

| Fichier .md | Cible PNG | Référencé dans |
|---|---|---|
| `wfc_paper_outputs.md` | `results/figures/schemas/wfc_paper_outputs.png` | introduction |
| `sample_to_output.md` | `results/figures/schemas/sample_to_output.png` | probleme_wfc |
| `overlap_compatibility.md` | `results/figures/schemas/overlap_compatibility.png` | approche_algorithmique |
| `wave_memory_layout.md` | `results/figures/schemas/wave_memory_layout.png` | implementation_cpp |
| `kokkos_architecture.md` | `results/figures/schemas/kokkos_architecture.png` | parallelisation_kokkos_gpu |
| `ue5_pipeline.md` | `results/figures/schemas/ue5_pipeline.png` | extensions_algorithmiques |
| `profile_breakdown.md` | `results/figures/schemas/profile_breakdown.png` | resultats_et_analyse |

## Workflow

1. Ouvrir le `.md` correspondant
2. Coller le prompt dans ChatGPT (DALL-E / image generation tool)
3. Sauvegarder le PNG dans `results/figures/schemas/`
4. Le `.tex` du rapport est déjà prêt à inclure le PNG (à remplacer
   les `\fbox{...}` placeholders par `\includegraphics`)

## Notes générales pour ChatGPT

- **Pas de texte parasite** : les schémas techniques doivent être lisibles, pas
  encombrés de paragraphes. ChatGPT a tendance à ajouter trop de texte.
- **Palette neutre** : noir / blanc / gris / un accent (bleu ou bordeaux),
  pour s'intégrer au thème du rapport (burgundy / gold).
- **Format** : 1600×900 ou 1200×900 (paysage), 300 DPI.
- **Style** : technique, propre, comme un schéma d'article scientifique.
