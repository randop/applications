require.config({
  shim: {
    van: {
      exports: "van", // Expose global 'van' as module
    },
  },
});

require(["van", "app"], function (van, app) {
  app.init(van);
});
