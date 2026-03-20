const sqlite3 = require('sqlite3').verbose();
const path = require('path');

const DB_PATH = path.join(__dirname, 'vnstat.db');
const db = new sqlite3.Database(DB_PATH);

// Interfaces
const interfaces = [
  { id: 2, name: 'enxe0' },
  { id: 4, name: 'wlan0' }
];

// Date range: January 14, 2026 to March 15, 2026
const startDate = new Date('2026-01-14');
const endDate = new Date('2026-03-15');

// Helper function to generate random traffic (in bytes)
function generateRandomTraffic(minGB, maxGB) {
  const minBytes = minGB * 1024 * 1024 * 1024;
  const maxBytes = maxGB * 1024 * 1024 * 1024;
  return Math.floor(Math.random() * (maxBytes - minBytes) + minBytes);
}

// Helper function to format date for SQLite
function formatDate(date) {
  return date.toISOString().split('T')[0];
}

// Helper function to format datetime for SQLite
function formatDateTime(date) {
  return date.toISOString().replace('T', ' ').substring(0, 19);
}

async function generateData() {
  console.log('Generating fake vnstat data...');
  console.log(`Date range: ${formatDate(startDate)} to ${formatDate(endDate)}`);
  console.log(`Interfaces: ${interfaces.map(i => i.name).join(', ')}`);

  // Clear existing data in the date range
  console.log('\nClearing existing data...');
  await new Promise((resolve, reject) => {
    db.run(`DELETE FROM day WHERE date >= ? AND date <= ?`,
      [formatDate(startDate), formatDate(endDate)],
      (err) => err ? reject(err) : resolve()
    );
  });

  await new Promise((resolve, reject) => {
    db.run(`DELETE FROM hour WHERE date >= ? AND date <= ?`,
      [formatDate(startDate), formatDate(endDate) + ' 23:59:59'],
      (err) => err ? reject(err) : resolve()
    );
  });

  await new Promise((resolve, reject) => {
    db.run(`DELETE FROM month WHERE date >= ? AND date <= ?`,
      [formatDate(startDate), formatDate(endDate)],
      (err) => err ? reject(err) : resolve()
    );
  });

  console.log('Existing data cleared.\n');

  // Generate daily data
  console.log('Generating daily data...');
  const dailyPromises = [];

  for (const iface of interfaces) {
    const currentDate = new Date(startDate);

    while (currentDate <= endDate) {
      const dateStr = formatDate(currentDate);

      // Generate realistic traffic patterns
      // Weekends have more traffic
      const dayOfWeek = currentDate.getDay();
      const isWeekend = dayOfWeek === 0 || dayOfWeek === 6;
      const baseMultiplier = isWeekend ? 1.5 : 1.0;

      // Random variation
      const variation = 0.7 + Math.random() * 0.6; // 0.7 to 1.3

      // RX (download) is typically higher than TX (upload)
      const rxGB = (2 + Math.random() * 8) * baseMultiplier * variation; // 2-10 GB download
      const txGB = (0.5 + Math.random() * 2) * baseMultiplier * variation; // 0.5-2.5 GB upload

      const rx = Math.floor(rxGB * 1024 * 1024 * 1024);
      const tx = Math.floor(txGB * 1024 * 1024 * 1024);

      dailyPromises.push(
        new Promise((resolve, reject) => {
          db.run(
            `INSERT INTO day (interface, date, rx, tx) VALUES (?, ?, ?, ?)`,
            [iface.id, dateStr, rx, tx],
            (err) => err ? reject(err) : resolve()
          );
        })
      );

      currentDate.setDate(currentDate.getDate() + 1);
    }
  }

  await Promise.all(dailyPromises);
  console.log(`✓ Generated ${dailyPromises.length} daily records`);

  // Generate hourly data
  console.log('Generating hourly data...');
  const hourlyPromises = [];

  for (const iface of interfaces) {
    const currentDate = new Date(startDate);

    while (currentDate <= endDate) {
      for (let hour = 0; hour < 24; hour++) {
        const hourDate = new Date(currentDate);
        hourDate.setHours(hour, 0, 0, 0);
        const dateTimeStr = formatDateTime(hourDate);

        // Traffic varies by hour (peak hours: 18-23, low: 02-06)
        let hourMultiplier = 1.0;
        if (hour >= 18 && hour <= 23) hourMultiplier = 1.8; // Evening peak
        else if (hour >= 2 && hour <= 6) hourMultiplier = 0.3; // Night low
        else if (hour >= 9 && hour <= 17) hourMultiplier = 1.2; // Work hours

        const rx = Math.floor(generateRandomTraffic(0.05, 0.8) * hourMultiplier);
        const tx = Math.floor(generateRandomTraffic(0.01, 0.2) * hourMultiplier);

        hourlyPromises.push(
          new Promise((resolve, reject) => {
            db.run(
              `INSERT INTO hour (interface, date, rx, tx) VALUES (?, ?, ?, ?)`,
              [iface.id, dateTimeStr, rx, tx],
              (err) => err ? reject(err) : resolve()
            );
          })
        );
      }

      currentDate.setDate(currentDate.getDate() + 1);
    }
  }

  await Promise.all(hourlyPromises);
  console.log(`✓ Generated ${hourlyPromises.length} hourly records`);

  // Generate monthly data
  console.log('Generating monthly data...');
  const monthlyPromises = [];

  // Calculate monthly totals from daily data
  const monthlyData = {};

  for (const iface of interfaces) {
    monthlyData[iface.id] = {};

    const currentDate = new Date(startDate);
    while (currentDate <= endDate) {
      const monthKey = currentDate.toISOString().substring(0, 7); // YYYY-MM

      if (!monthlyData[iface.id][monthKey]) {
        monthlyData[iface.id][monthKey] = { rx: 0, tx: 0 };
      }

      // Simulate daily accumulation
      const dayOfWeek = currentDate.getDay();
      const isWeekend = dayOfWeek === 0 || dayOfWeek === 6;
      const baseMultiplier = isWeekend ? 1.5 : 1.0;
      const variation = 0.7 + Math.random() * 0.6;

      const rxGB = (2 + Math.random() * 8) * baseMultiplier * variation;
      const txGB = (0.5 + Math.random() * 2) * baseMultiplier * variation;

      monthlyData[iface.id][monthKey].rx += Math.floor(rxGB * 1024 * 1024 * 1024);
      monthlyData[iface.id][monthKey].tx += Math.floor(txGB * 1024 * 1024 * 1024);

      currentDate.setDate(currentDate.getDate() + 1);
    }

    // Insert monthly records
    for (const [monthKey, data] of Object.entries(monthlyData[iface.id])) {
      const monthDate = `${monthKey}-01`;
      monthlyPromises.push(
        new Promise((resolve, reject) => {
          db.run(
            `INSERT INTO month (interface, date, rx, tx) VALUES (?, ?, ?, ?)`,
            [iface.id, monthDate, data.rx, data.tx],
            (err) => err ? reject(err) : resolve()
          );
        })
      );
    }
  }

  await Promise.all(monthlyPromises);
  console.log(`✓ Generated ${monthlyPromises.length} monthly records`);

  // Update interface totals
  console.log('\nUpdating interface totals...');
  for (const iface of interfaces) {
    const totalRx = Object.values(monthlyData[iface.id] || {}).reduce((sum, data) => sum + data.rx, 0);
    const totalTx = Object.values(monthlyData[iface.id] || {}).reduce((sum, data) => sum + data.tx, 0);

    await new Promise((resolve, reject) => {
      db.run(
        `UPDATE interface SET rxtotal = rxtotal + ?, txtotal = txtotal + ?, updated = ? WHERE id = ?`,
        [totalRx, totalTx, formatDate(new Date()), iface.id],
        (err) => err ? reject(err) : resolve()
      );
    });
  }
  console.log('✓ Interface totals updated');

  console.log('\n✅ Fake data generation complete!');
  console.log(`Generated data for ${interfaces.length} interfaces from ${formatDate(startDate)} to ${formatDate(endDate)}`);

  db.close();
}

generateData().catch(err => {
  console.error('Error generating data:', err);
  db.close();
  process.exit(1);
});
