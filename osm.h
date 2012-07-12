/*
 * osmrail - OpenStreetMap filter for railway-related features
 * Copyright (C) 2011 Paul D Kelly
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#define OSM_TAG_SIZE 255 /**< Maximum length of key/value strings in OSM tags */

/**
 * \brief Structure describing an OSM key/value attribute tag
 * 
 * All OSM features (nodes, ways, relations) may have an unlimited number
 * of key/value tags attached to them. The key and value are freeform UTF-8
 * text and may be up to 255 characters long each.
 */
struct osm_tag
{
    char key[OSM_TAG_SIZE + 1];   /**< Key string */
    char value[OSM_TAG_SIZE + 1]; /**< Value string */
};

/**
 * \brief Structure describing an OSM node
 * 
 * A node is a single point, identified by WGS84 latitude, longitude 
 * co-ordinates. It has an associated unique ID which is an unsigned 64-bit
 * integer. An unlimited amount of key/value tags may be associated with
 * each node.
 * 
 * Nodes may form part of ways and relations, where they are identified by
 * their unique ID number.
 *
 * Note: Node attributes are not yet supported in this implementation, 
 * only location.
 */
struct osm_node
{
    double lat;           /**< WGS84 latitude of node */
    double lon;           /**< WGS84 longitude of node */
    struct osm_tag *tags; /**< Array of "tag_count" attribute tags attached to this node */
    int tag_count;        /**< Number of key/value attribute tags attached to this node */
    unsigned int id;      /**< Unique node ID. Should be upgraded to 64-bit integer soon. */
};

/**
 * \brief Structure describing an OSM way
 * 
 * A way is defined as an array of at least two nodes. An unlimited amount of
 * key/value tages may be associated with each way.
 */
struct osm_way
{
    int node_count;       /**< Number of nodes this way is composed of */
    int tag_count;        /**< Number of key/value attribute tags attached to this way */
    unsigned int *nodes;  /**< Array of "node_count" node IDs that form this way */
    struct osm_tag *tags; /**< Array of "tag_count" attribute tags attached to this way */
    unsigned int id;      /**< Unique way ID. 32-bit unsigned integer. */
};

/**
 * \brief Structure describing an OSM relation
 * 
 * A relation groups certain nodes and/or ways together. Each node or way may
 * have a "role" in the relationship, which is defined by a string. An 
 * unlimited amount of key/value tags may be associated with each relation.
 */
struct osm_relation
{
    int node_count; /**< Number of nodes this relation contains */
    int way_count;  /**< Number of ways this relation contains */
    int tag_count;  /**< Number of key/value attribute tags attached to this relation */
    unsigned int *nodes;  /**< Array of "node_count" node IDs that form this way */
    /** Array of "node_count" strings defining the role of each node in the relation */
    char (*node_roles)[OSM_TAG_SIZE + 1];
    unsigned int *ways;   /**< Array of "way_count" way IDs that form this way */
    /** Array of "way_count" strings defining the role of each way in the relation */
    char (*way_roles)[OSM_TAG_SIZE + 1];
    struct osm_tag *tags; /**< Array of "tag_count" attribute tags attached to this way */
    unsigned int id;      /**< Unique relation ID. 32-bit unsigned integer. */
};

/** Callback for processing nodes */
typedef void osm_node_callback_t(struct osm_node *, void *);
/** Callback for processing ways */
typedef void osm_way_callback_t(struct osm_way *, void *);
/** Callback for processing relations */
typedef void osm_relation_callback_t(struct osm_relation *, void *);

/* osm_planet.c */

/**
 * \brief Open a bzip2-compressed OpenStreetMap planet file
 * 
 * \param filename Full path to the bzip2-compressed file
 * 
 * \return
 *   On successful opening of the file, pointer to a struct osm_planet object 
 *   which should be passed in subsequent calls to osm_planet_*() functions.
 *   NULL on failure to open the file.
 */
struct osm_planet *osm_planet_open(const char *filename);

/**
 * \brief Read a line of text from the OSM planet file
 * 
 * The line will be returned as a null-terminated string with any carriage
 * return or newline characters stripped out.
 * 
 * \param osf
 *   Pointer to struct osm_planet object, as previously obtained from a call
 *   to osm_planet_init().
 * \param line
 *   Pointer to pointer to char, into which a pointer to the string containing
 *   the line of text retrieved from the server will be placed.
 * 
 * \return
 *   0 if a line of text was successfully read from the server, 2 if EOF was
 *   received, otherwise 1
 */
int osm_planet_readln(struct osm_planet *osf, char **line);

/**
 * \brief Close OpenStreetMap planet file and decompressor
 * 
 * \param osf
 *   Pointer to struct osm_planet object, as previously obtained from a call
 *   to osm_planet_init().
 *
 * \return
 *   1 if there was an error closing the file, otherwise 0
 */
int osm_planet_close(struct osm_planet *osf);

/* osm_parse.c */

/**
 * \brief Initialise the OpenStreetMap (API v0.6) XML parser
 * 
 * \param cb_node
 *   Function pointer to callback function that will be called every time
 *   a node is parsed from the OSM data
 * \param cb_way
 *   Function pointer to callback function that will be called every time
 *   a way is parsed from the OSM data
 * \param priv_data
 *   Pointer to private data that will be passed as the second argument to
 *   the callback functions whenever they are called
 * 
 * \return
 *   Pointer to a struct osm_parse object, which should be passed in
 *   subsequent calls to osm_parse_*() functions
 */
struct osm_parse *osm_parse_init(osm_node_callback_t *, osm_way_callback_t *, osm_relation_callback_t *, void *);

/**
 * \brief Ingest a single line of OpenStreetMap (API v0.6) XML data
 *
 * If the line ingested completes the description of a node or way, the appropriate
 * callback will be triggered in order to parse the node/way.
 * 
 * \param parse
 *   Pointer to struct osm_parse object as obtained from a previous call
 *   to osm_parse_init()
 * \param
 *   Line of OpenStreetMap XML output to ingest as a null-terminated string, 
 *   without CR/LF characters
 * 
 * \return
 *   1 if this line marks the end of OSM data contained within an XML document,
 *   otherwise 0
 */
int osm_parse_ingest(struct osm_parse *parse, char *line);

/**
 * \brief Destroy a struct osm_parse object and free the memory used by it
 * 
 * \param parse
 *   Pointer to struct osm_parse object, as previously obtained from a call
 *   to osm_parse_init
 */
void osm_parse_destroy(struct osm_parse *parse);
