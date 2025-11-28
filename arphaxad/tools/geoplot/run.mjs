import { readFile, writeFile } from 'node:fs/promises';
import { createReadStream } from 'node:fs';
import { createInterface } from 'node:readline';

const log = console;

const geoJson = {
  "type": "FeatureCollection",
  "name": "Drive Tracker",
  "features": [
    {
      "type": "Feature",
      "properties": {
        "name": "Manila drive",
        "mode": "road",
        "description": "SM Cherry Congressional Avenue, Quezon City, Philippines",
        "stroke": "#ff0000",
        "stroke-width": 8,
        "stroke-opacity": 1
      },
      "geometry": {
        "type": "LineString",
        "coordinates": []
      }
    }
  ]
};

let doExample = false;
if (doExample) {
    // index 0 is longitude
    // index 1 is latitude
    geoJson.features[0].geometry.coordinates.push([121.03868100,14.67169350]);
    geoJson.features[0].geometry.coordinates.push([121.03212683,14.68165100]);
    log.info(geoJson);
}

async function processIotDriveLines() {
  const rl = createInterface({
    input: createReadStream('iot-drive.logs', { encoding: 'utf-8' }),
    crlfDelay: Infinity
  });

  for await (const line of rl) {
    const trimmed = line.trim();
    const fields = trimmed.split(';');
    const dataJson = JSON.parse(fields[1]);
    geoJson.features[0].geometry.coordinates.push([dataJson.longitude, dataJson.latitude]);
  }

  log.info("Done: processIotDriveLines");
}

try {
    await processIotDriveLines();
} catch (err) {
    log.error('Error reading file:', err.message);
    process.exit(1);
}

const filename = "drive.geojson";

try {
    await writeFile(filename, JSON.stringify(geoJson), { encoding: 'utf-8' });
    log.info(`File "${filename}" written successfully!`);
    log.info(`Content (${text.length} chars).`);
} catch (err) {
    log.error('Failed to write file:', err.message);
    process.exit(1);
}
