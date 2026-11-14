# Plan : options JPEG et PNG dans Export Image

Ce document est une proposition d'implémentation. Aucune fonctionnalité décrite ci-dessous n'est encore livrée.

## Objectif

Permettre de régler la compression JPEG et le chargement progressif, de choisir la transparence et l'entrelacement PNG, et de lire la taille du fichier préparé en mégabytes. L'export produit une image aplatie : il ne modifie ni le document, ni son historique, ni le format du projet.

## Comportement attendu

### JPEG

- Conserver le curseur existant de qualité **0 à 100**, son défaut actuel de 85 et la préférence `jpegExportQuality`. Une valeur plus faible privilégie un fichier plus léger ; JPEG reste un format avec perte. Cette échelle n'est pas présentée comme équivalente à celle de Photoshop, graduée de 0 à 12.
- Ajouter un choix exclusif **Standard**, **Optimized**, **Progressive**. Standard vise la compatibilité ; Optimized utilise des tables Huffman optimisées ; Progressive organise les données en plusieurs passages pour permettre un affichage progressif. Garder Standard par défaut et ne jamais produire silencieusement un autre mode que celui sélectionné.
- Conserver le choix RGB ou CMYK et le profil CMYK, le fond de transparence choisi, le profil ICC et la résolution. L'aperçu doit provenir du JPEG effectivement encodé avec ces options.

### PNG

- Proposer pour le PNG ordinaire une option de transparence, activée par défaut, et une option d'entrelacement, désactivée par défaut. Sans transparence, aplatir sur le fond choisi. L'entrelacement concerne l'affichage pendant le téléchargement ; il ne change pas les pixels décodés.
- Ne pas appliquer ces options à Channel Pack : ses quatre canaux doivent conserver leurs valeurs exactes. La réduction de palette PNG-8 est hors périmètre, car elle peut modifier couleurs et transparence.

### Taille, aperçu et annulation

- Pour **tous les formats** disponibles, afficher la taille réelle du fichier préparé en mégabytes décimaux : 1 Mb = 1 000 000 octets. Employer exactement le suffixe demandé **Mb**, même si cette notation désigne habituellement des mégabits. Exemple : `1.23 Mb`. Afficher `< 0.01 Mb` pour une taille non nulle inférieure à 10 000 octets ; sinon arrondir à deux décimales.
- Chaque modification pertinente invalide le résultat précédent, désactive Save, annule ou remplace le travail en cours, puis met à jour aperçu et taille uniquement pour la génération courante. Save copie exactement le fichier présenté.
- Cancel, fermeture, échec d'encodage ou de copie ne modifient pas le document ni son historique. Les fichiers temporaires sont nettoyés et une destination existante reste récupérable. Un résultat périmé ne peut pas réactiver Save.

## État actuel et points d'entrée

- `src/ui/FileActions.cpp`, `MainWindow::exportImage()` : dialogue ouvert depuis **File → Export Image** et **Ctrl+Shift+E**, snapshot du document, qualité persistée, génération asynchrone avec temporisation, aperçu, taille en octets, annulation et copie du fichier préparé.
- `src/imaging/image_types.h` : `ExportOptions` transporte format, qualité, fond, résolution, profil et annulation.
- `src/imaging/stream_export.cpp` : encodage WIC par bandes, conversion RGB/CMYK, ICC et DPI, fichier temporaire, aperçu JPEG décodé et copie sécurisée.
- `src/imaging/wic_codec.cpp` : second encodeur en mémoire ; vérifier ses appelants avant de modifier le contrat partagé et refuser explicitement toute option non prise en charge.
- `tests/export_stream/ExportStreamTests.cpp`, `tests/export_stream/ExportUiTests.cpp` et `tests/cmyk/CmykContracts.cpp` couvrent des chemins existants à prolonger. Le format `.comp` et la pile d'annulation ne nécessitent aucune migration.

## Choix technique et points à établir

1. Vérifier sur la version de Windows prise en charge les options réellement offertes par WIC pour JPEG standard, Huffman optimisé, progressif et PNG entrelacé. Examiner les fichiers produits, sans supposer que l'écriture d'une propriété est suffisante.
2. Prendre **libjpeg-turbo** comme candidat pour JPEG si WIC ne fournit pas les trois modes de manière fiable. Il propose l'encodage standard, l'optimisation Huffman et le progressif. Comparer également MozJPEG si le gain de taille justifie son temps d'encodage. Ne pas affirmer qu'un encodeur est le plus optimisé avant mesure.
3. Comparer sur Windows x64 des photos, dégradés, textes et compositions transparentes : taille à qualité visuelle comparable, temps, mémoire maximale et compatibilité de décodage. Tester RGB et CMYK. Documenter le choix, la version et les résultats. Références : [libjpeg-turbo et MozJPEG](https://www.libjpeg-turbo.org/About/Mozjpeg), [MozJPEG](https://github.com/mozilla/mozjpeg).
4. Vérifier la mémoire réellement utilisée par l'encodeur, notamment les coefficients d'un JPEG progressif : une API par lignes ne garantit pas une mémoire bornée. Mesurer jusqu'à la limite actuelle de 100 millions de pixels, définir un budget et signaler les cas non pris en charge avant tout remplacement de destination.
5. Évaluer WIC pour PNG entrelacé ; si insuffisant, examiner libpng avec les mêmes exigences de mémoire, d'annulation et de fichiers temporaires. Toute dépendance retenue doit être épinglée, intégrée au build et aux paquets, et accompagnée de ses licences et sources.

## Étapes d'implémentation

1. **Établir la référence** : relever le comportement WIC actuel, mesurer les candidats et choisir le ou les encodeurs et leurs limites. Vérification : résultats reproductibles et fichiers décodables par un lecteur indépendant.
2. **Étendre les options** : ajouter des types explicites dans `ExportOptions`, valider chaque combinaison et tracer tous les consommateurs, notamment `WicCodec::encode`. Vérification : aucune option ignorée silencieusement.
3. **Implémenter l'encodage** : raccorder le backend retenu au chemin par bandes et conserver les conversions, profils, DPI, annulation, limites et remplacement de destination. Vérification : marqueurs JPEG et entrelacement PNG conformes, pixels et métadonnées décodés corrects.
4. **Adapter le dialogue** : ajouter les contrôles propres au format et l'affichage en Mb dans `FileActions.cpp`, connecter leurs changements au cycle de génération existant et conserver la préférence JPEG. Vérification : aperçu, taille et fichier enregistré correspondent à la dernière génération.
5. **Documenter et vérifier** : actualiser `docs/user-guide.md`, exécuter les tests ciblés, effectuer les contrôles Windows réels et préparer les captures pour la PR. Vérification : commandes et résultats effectifs consignés sans revendiquer de compatibilité Photoshop ou Mac non mesurée.

## Plan de test automatisé

- Construire avec le preset `windows-x64-release` documenté, puis exécuter les tests concernés : `ctest --preset windows-x64-release -R "^(export\\.ui|export_stream\\.|migration\\.cmyk_)" --output-on-failure`. Ajouter les cas pertinents aux cibles existantes et exécuter tout nouveau cas associé.
- Avec des fichiers réellement exportés, vérifier les marqueurs JPEG et le nombre de passages, le décodage indépendant, RGB/CMYK, ICC, DPI, fond et limites de qualité. Vérifier les pixels PNG avec et sans alpha et avec ou sans entrelacement ; contrôler séparément Channel Pack.
- Dans le dialogue de production, vérifier les contrôles visibles selon le format, la restauration de la qualité, les changements rapides, l'annulation et les erreurs, l'absence de mutation du document et de l'historique, et l'identité des octets affichés/préparés/enregistrés.
- Vérifier la conversion de taille pour chaque format, un fichier sous 0,01 Mb, les seuils d'arrondi et un changement de réglage. Vérifier l'absence de résultat périmé et la conservation d'une destination existante après annulation ou échec.
- Mesurer temps et mémoire sur petits et grands documents, y compris le JPEG progressif ; reporter le matériel, les entrées, les limites et les résultats réellement observés.

## Vérification manuelle et livraison

Après implémentation et tests, créer une branche de fonctionnalité à partir de `main` à jour en préservant le travail local, committer et pousser les seuls changements concernés, puis mettre à jour cette PR sans la fusionner. Lancer une fois la version finale et la laisser ouverte.

Dans CompositorWindows : ouvrir un document avec zones transparentes et détails fins ; ouvrir **File → Export Image** ; essayer les trois modes JPEG et plusieurs qualités, puis PNG avec les deux options ; vérifier aperçu, taille en Mb et fichier enregistré. Pendant un encodage, changer rapidement de réglage puis annuler ; vérifier que le document reste inchangé. Contrôler le dialogue avec la mise à l'échelle Windows utilisée et faire une capture réelle montrant les réglages et la taille. Consigner séparément toute vérification impossible.

## État de cette proposition

Lecture du code et des tests existants uniquement. Aucun benchmark, build, test automatisé ou contrôle visuel natif n'a été exécuté pour ce plan.
