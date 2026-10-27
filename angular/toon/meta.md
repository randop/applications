# metadata
```
<?xml version="1.0" encoding="UTF-8"?>
<anime>
    <title></title>

    <originalTitle></originalTitle>

    <alternativeTitle></alternativeTitle>

    <releaseDate></releaseDate>

    <genres>
        <genre></genre>
    </genres>

    <images>
        <image id="" type="">
            <url></url>
            <alt></alt>
        </image>
    </images>

    <synopsis></synopsis>
</anime>
```

## Metadata field definitions

| XML element        | Data type | Description                                       |
| ------------------ | --------- | ------------------------------------------------- |
| `anime`            | Object    | Root element containing all anime metadata        |
| `title`            | String    | Main or display title                             |
| `originalTitle`    | String    | Title in its original language                    |
| `alternativeTitle` | String    | Other title or romanized title                    |
| `releaseDate`      | Date      | Original release date, preferably `YYYY-MM-DD`    |
| `genres`           | Array     | Collection of genre values                        |
| `genre`            | String    | Individual genre                                  |
| `images`           | Array     | Collection of image references                    |
| `image`            | Object    | Metadata for one image                            |
| `image/@id`        | String    | Unique identifier for the image within the record |
| `image/@type`      | String    | Image category, such as `cover` or `poster`       |
| `url`              | URI       | Address of the image resource                     |
| `alt`              | String    | Alternative text describing the image             |
| `synopsis`         | String    | Brief description of the anime                    |

```
# Anime Metadata

- **Title:** Main display title
- **Original Title:** Original-language title
- **Alternative Title:** Alternate or romanized title
- **Release Date:** Original release date in YYYY-MM-DD format
- **Genres:** List of genres
- **Images:** Image URLs, image types, identifiers, and alternative text
- **Synopsis:** Brief description of the anime
```
