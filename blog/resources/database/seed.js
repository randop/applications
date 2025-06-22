// Connect to the database specified by MONGO_INITDB_DATABASE
db = db.getSiblingDB("localhost");

//*****************************************************************************
// Create modes
//*****************************************************************************
db.createCollection("modes");
// Create a unique index on the 'id' field
db.modes.createIndex({ id: 1 }, { unique: true });
db.modes.insertMany([
  { id: 1, mode: "markdown" },
  { id: 2, mode: "html" },
  { id: 3, mode: "plain" },
]);

//*****************************************************************************
// Create layouts
//*****************************************************************************
db.createCollection("layouts");
// Create a unique index on the 'id' field
db.layouts.createIndex({ id: 1 }, { unique: true });
db.layouts.insertMany([
  { id: 1, header: "<html><body>", footer: "</body></html>" },
]);

//*****************************************************************************
// Create pages
//*****************************************************************************
db.createCollection("pages");
// Create a unique index on the 'id' field
db.pages.createIndex({ id: 1 }, { unique: true });
const indexPageContent = `
<h1>Hello World</h1>
<p>Lorem ipsum dolor sit amet, consectetur adipiscing elit. Nulla ornare suscipit justo, vel tempor ligula pellentesque ac. Sed at justo lacinia, mollis urna viverra, tincidunt est. Nunc vehicula aliquam eros eu ullamcorper. Aenean placerat dui at posuere dictum. Fusce nec massa eu urna accumsan feugiat a in dolor. Suspendisse nulla elit, aliquet in nibh nec, iaculis tempor leo. Proin ipsum libero, dictum sed placerat quis, ultricies sed ante. Nullam luctus, neque at laoreet cursus, nulla ante egestas lacus, vitae rutrum orci risus sit amet lacus.</p>
<p>Etiam nibh massa, ornare vitae tristique et, tristique a lacus. Integer ac molestie erat. Donec sit amet massa dui. Proin sit amet sollicitudin ligula. Cras pretium auctor dui, sed consequat urna egestas sed. Vestibulum volutpat lorem quis tellus commodo auctor. Vivamus gravida commodo odio et mollis. Sed euismod interdum leo, bibendum volutpat sapien elementum at.</p>
  `;
db.pages.insertMany([
  {
    id: "index",
    createdAt: { $date: "2025-06-22T23:02:00Z" },
    updatedAt: { $date: "2025-06-22T23:02:00Z" },
    modeId: 2,
    layoutId: 1,
    title: "hello",
    content: indexPageContent,
  },
]);
