:man_page: mongoc_client_get_database_names

mongoc_client_get_database_names()
==================================

.. warning::
   .. deprecated:: 1.9.0

      Use :symbol:`mongoc_client_get_database_names_with_opts()` instead.

Synopsis
--------

.. code-block:: c

  char **
  mongoc_client_get_database_names (mongoc_client_t *client, bson_error_t *error);

Description
-----------

This function queries the MongoDB server for a list of known databases.

.. include:: includes/retryable-read.txt

Parameters
----------

* ``client``: A :symbol:`mongoc_client_t`.
* ``error``: An optional location for a :symbol:`bson_error_t <errors>` or ``NULL``.

Errors
------

Errors are propagated via the ``error`` parameter.

Returns
-------

A ``NULL`` terminated vector of ``NULL-byte`` terminated strings. The result should be freed with :symbol:`bson:bson_strfreev()`.

``NULL`` is returned upon failure and ``error`` is set.
