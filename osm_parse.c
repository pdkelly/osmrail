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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "osm.h"

struct osm_parse
{
    osm_node_callback_t *cb_node;         /**< Callback function for nodes */
    osm_way_callback_t *cb_way;           /**< Callback functions for ways */
    osm_relation_callback_t *cb_relation; /**< Callback functions for relations */
    void *priv_data; /**< Private data to be passed to callback functions */

    struct osm_node node;
    int max_node_tags;
    struct osm_way way;
    int max_way_nodes, max_way_tags;
    struct osm_relation relation;
    int max_relation_nodes, max_relation_ways, max_relation_tags;

    /* Parse state flags */
    char in_osm, in_node, in_way, in_relation;
};

static int parse_tag(const char *text, struct osm_tag *);
static void read_string(char *dest, char **ptr);
static char deescape_xml(char **str);

struct osm_parse *osm_parse_init(osm_node_callback_t *cb_node, osm_way_callback_t *cb_way, 
                                 osm_relation_callback_t *cb_relation, void *priv_data)
{
    struct osm_parse *parse = calloc(1, sizeof(struct osm_parse));

    parse->cb_node = cb_node;
    parse->cb_way = cb_way;
    parse->cb_relation = cb_relation;
    parse->priv_data = priv_data;

    return parse;
}

int osm_parse_ingest(struct osm_parse *parse, char *line)
{
    char *ptr, *tag;
    char end_tag = 0;

    /* Locate opening angle bracket of XML tag */
    ptr = strchr(line, '<');
    if(!ptr) /* No XML tags on this line */
        return 0;

    /* Advance to first character inside tag and skip any spaces */
    ptr++; 
    while(isspace(*ptr))
        ptr++;

    if(*ptr == '/')
    {
        end_tag = 1;
        ptr++;
    }

    tag = ptr;
    /* Mark end of tag with terminating NULL */
    while(!isspace(*ptr) && *ptr != '/' && *ptr != '>')
        ptr++;
    *ptr = '\0';

    /* Locate start of <osm></osm> data block */
    if(!parse->in_osm)
    {
        if(strcmp(tag, "osm") == 0)
        {
            parse->in_osm = 1;

            /* TODO: Parse out copyright and attribution details here
             * and use them somehow. */
        }

        return 0;
    }

    if(parse->in_node)
    {
        struct osm_node *node = &parse->node;

        if(end_tag && strcmp(tag, "node") == 0)
        {
            /* End of node; process using callback if specified */
            parse->in_node = 0;

            if(parse->cb_node)
                parse->cb_node(node, parse->priv_data);

            return 0;
        }

        /* Attribute tag for node */
        if(strcmp(tag, "tag") == 0)
        {
            ptr = tag+4;

            if(node->tag_count >= parse->max_node_tags)
            {
                parse->max_node_tags += 5;
                node->tags = realloc(node->tags, parse->max_node_tags * sizeof(struct osm_tag));
            }

            if(parse_tag(ptr, &node->tags[node->tag_count]) != 0)
            {
                fprintf(stderr, "osm_parse_ingest(): Error parsing node tag; line follows below\n%s\n", ptr);
                return 0;
            }

            node->tag_count++;
        }

    }
    else if(parse->in_way)
    {
        struct osm_way *way = &parse->way;

        if(end_tag && strcmp(tag, "way") == 0)
        {
            /* End of way; process using callback if specified */
            parse->in_way = 0;

            if(parse->cb_way)
                parse->cb_way(way, parse->priv_data);

            return 0;
        }

        /* Node forming part of way */
        if(strcmp(tag, "nd") == 0)
        {
            ptr = tag+3;

            if(way->node_count >= parse->max_way_nodes)
            {
                parse->max_way_nodes += 10;
                way->nodes = realloc(way->nodes, parse->max_way_nodes * sizeof(unsigned int));
            }
            if(sscanf(ptr, "ref=\"%u\"", &way->nodes[way->node_count]) != 1)
            {
                fprintf(stderr, "osm_parse_ingest(): Error parsing way member node; line follows below\n%s\n", ptr);
                return 0;
            }
            way->node_count++;
        }
        /* Attribute tag for way */
        else if(strcmp(tag, "tag") == 0)
        {
            ptr = tag+4;

            if(way->tag_count >= parse->max_way_tags)
            {
                parse->max_way_tags += 5;
                way->tags = realloc(way->tags, parse->max_way_tags * sizeof(struct osm_tag));
            }

            if(parse_tag(ptr, &way->tags[way->tag_count]) != 0)
            {
                fprintf(stderr, "osm_parse_ingest(): Error parsing way tag; line follows below\n%s\n", ptr);
                return 0;
            }

            way->tag_count++;
        }
    }
    else if(parse->in_relation)
    {
        struct osm_relation *rel = &parse->relation;

        if(end_tag && strcmp(tag, "relation") == 0)
        {
            /* End of relation; process using callback if specified */
            parse->in_relation = 0;

            if(parse->cb_relation)
                parse->cb_relation(rel, parse->priv_data);

            return 0;
        }

        /* Member (either node or way) of relation */
        if(strcmp(tag, "member") == 0)
        {
            ptr = tag+7;

            /* Member node */
            if(strncmp(ptr, "type=\"node\"", 11) == 0)
            {
                ptr += 12;

                if(rel->node_count >= parse->max_relation_nodes)
                {
                    parse->max_relation_nodes += 10;
                    rel->nodes = realloc(rel->nodes, parse->max_relation_nodes * sizeof(unsigned int));
                    rel->node_roles = realloc(rel->node_roles, parse->max_relation_nodes * (OSM_TAG_SIZE + 1));
                }

                if(sscanf(ptr, "ref=\"%u\"", &rel->nodes[rel->node_count]) != 1)
                {
                    fprintf(stderr, "osm_parse_ingest(): Error parsing relation member node; line follows below\n%s\n", ptr);
                    return 0;
                }
                ptr = strstr(ptr, "role=\"");
                if(!ptr)
                {
                    fprintf(stderr, "osm_parse_ingest(): Error parsing relation member node; line follows below\n%s\n", ptr);
                    return 0;
                }
                ptr += 6;
                read_string((char *)rel->node_roles[rel->node_count], &ptr);

                rel->node_count++;
            }
            /* Member way */
            else if(strncmp(ptr, "type=\"way\"", 10) == 0)
            {
                ptr += 11;

                if(rel->way_count >= parse->max_relation_ways)
                {
                    parse->max_relation_ways += 10;
                    rel->ways = realloc(rel->ways, parse->max_relation_ways * sizeof(unsigned int));
                    rel->way_roles = realloc(rel->way_roles, parse->max_relation_ways * (OSM_TAG_SIZE + 1));
                }

                if(sscanf(ptr, "ref=\"%u\"", &rel->ways[rel->way_count]) != 1)
                {
                    fprintf(stderr, "osm_parse_ingest(): Error parsing relation member way; line follows below\n%s\n", ptr);
                    return 0;
                }
                ptr = strstr(ptr, "role=\"");
                if(!ptr)
                {
                    fprintf(stderr, "osm_parse_ingest(): Error parsing relation member way; line follows below\n%s\n", ptr);
                    return 0;
                }
                ptr += 6;
                read_string((char *)rel->way_roles[rel->way_count], &ptr);

                rel->way_count++;
            }           
        }
        /* Attribute tag for relation */
        else if(strcmp(tag, "tag") == 0)
        {
            ptr = tag+4;

            if(rel->tag_count >= parse->max_relation_tags)
            {
                parse->max_relation_tags += 5;
                rel->tags = realloc(rel->tags, parse->max_relation_tags * sizeof(struct osm_tag));
            }

            if(parse_tag(ptr, &rel->tags[rel->tag_count]) != 0)
            {
                fprintf(stderr, "osm_parse_ingest(): Error parsing relation tag; line follows below\n%s\n", ptr);
                return 0;
            }

            rel->tag_count++;
        }
    }
    else if(strcmp(tag, "node") == 0)
    {
        struct osm_node *node = &parse->node;

        ptr = tag+5;

        node->tag_count = 0;
        if(sscanf(ptr, "id=\"%u\" lat=\"%lf\" lon=\"%lf\"", &node->id,  &node->lat, &node->lon) != 3)
        {
            fprintf(stderr, "osm_parse_ingest(): Error parsing node; line follows below\n%s\n", ptr);
            return 0;
        }

        if(strstr(ptr, "/>"))
        {
            /* End of node; process using callback if specified */
            if(parse->cb_node)
                parse->cb_node(node, parse->priv_data);
        }
        else /* multi-line node */
            parse->in_node = 1;

        return 0;
    }
    else if(strcmp(tag, "way") == 0)
    {
        struct osm_way *way = &parse->way;

        ptr = tag+4;

        way->node_count = way->tag_count = 0;
        if(sscanf(ptr, "id=\"%u\"", &way->id) != 1)
        {
            fprintf(stderr, "osm_parse_ingest(): Error parsing way; line follows below\n%s\n", ptr);
            return 0;
        }

        if(strstr(ptr, "/>")) /* end of way */
            ; /* do nothing since this way must have no member nodes nor tags */
        else /* normal multi-line way */
            parse->in_way = 1;

        return 0;
    }
    else if(strcmp(tag, "relation") == 0)
    {
        struct osm_relation *rel = &parse->relation;

        ptr = tag+9;

        rel->node_count = rel->way_count = rel->tag_count = 0;
        if(sscanf(ptr, "id=\"%u\"", &rel->id) != 1)
        {
            fprintf(stderr, "osm_parse_ingest(): Error parsing relation; line follows below\n%s\n", ptr);
            return 0;
        }

        if(strstr(ptr, "/>")) /* end of relation */
            ; /* do nothing since this relation must have no member nodes, ways nor tags */
        else /* normal multi-line relation */
            parse->in_relation = 1;

        return 0;
    }
    else if(end_tag && strcmp(tag, "osm") == 0) /* end of data block */
        return 1;

    return 0;
}

void osm_parse_destroy(struct osm_parse *parse)
{
    free(parse->node.tags);
    free(parse->way.nodes);
    free(parse->way.tags);
    free(parse->relation.nodes);
    free(parse->relation.ways);
    free(parse->relation.tags);
    free(parse);

    return;
}

static int parse_tag(const char *text, struct osm_tag *tag)
{
    char *ptr;

    /* Key */
    ptr = strstr(text, "k=\"");
    if(!ptr)
        return -1;
    ptr += 3;
    read_string(tag->key, &ptr);

    /* Value */
    ptr = strstr(ptr, "v=\"");
    if(!ptr)
        return -1;
    ptr += 3;
    read_string(tag->value, &ptr);

    return 0;
}

/* Parse a string from an OSM tag, allowing for the case where it is
 * an empty string, and de-escaping XML quoted characters. */
static void read_string(char *dest, char **ptr)
{
    int c, len = 0;

    while((c = *(*ptr)++) != '\"' && len < OSM_TAG_SIZE)
    {
        if(c == '&')
            dest[len++] = deescape_xml(ptr);
        else
            dest[len++] = c;
    }
    dest[len] = '\0';
    
    return;
}

/* De-escape the 5 standard XML-escaped characters:
 *               ' " & < >
 */
static char deescape_xml(char **str)
{
    switch(*(*str))
    {
        case 'a':
            switch(*(*str+1))
            {
                case 'm': /* &amp; */
                    (*str) += 4;
                    return '&';
                case 'p': /* &apos; */
                    (*str) += 5;
                    return '\'';
            }
        case 'g': /* &gt; */
            (*str) += 3;
            return '>';
        case 'l': /* &lt; */
            (*str) += 3;
            return '<';
        case 'q': /* &quot; */
            (*str) += 5;
            return '\"';
        default: /* unrecognised; don't deescape */
            return '&';
    }
}
