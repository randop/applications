// Put db user to avoid error:
// ERROR [MongooseModule] Unable to connect to the database.
// MongoServerError: Authentication failed.

// Connect to VRP database declared on MONGO_INITDB_DATABASE
db = db.getSiblingDB('vrp');
db.createUser({
  user: 'user',
  pwd: 'pass',
  roles: [
    { role: 'readWrite', db: 'vrp' },
  ]
});

//*****************************************************************************
// Task Groups presets
//*****************************************************************************
db.createCollection("taskgroups");

db.taskgroups.insertMany([
  { label: "To Do" },
  { label: "In Progress" },
  { label: "Done" },
]);

