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

#include "osm.h"

#define OSM_NODE     0
#define OSM_WAY      1
#define OSM_RELATION 2

struct osm_params
{
    /* Tags of interest */
    struct osm_tag *tags;
    int tag_count;

    /* IDs of nodes/ways/relations of interest */
    unsigned int *ids[3];
    int count[3];
    int max[3];
};

static int parse_entire_file(char *filename, osm_node_callback_t *, osm_way_callback_t *, 
			     osm_relation_callback_t *, void *);
static osm_node_callback_t     load_node, output_node;
static osm_way_callback_t      load_way_1, load_way_2, output_way;
static osm_relation_callback_t load_relation, output_relation;
static void sort_ids(struct osm_params *, int ele);

int main(int argc, char **argv)
{
    struct osm_params *osm = calloc(1, sizeof(struct osm_params));

    if(argc != 2)
    {
        fprintf(stderr, "Usage: %s <planet.osm.bz2>\n", argv[0]);
        return 1;
    }

    /* Define tags of interest. In future these could be specified on standard input. */
    osm->tag_count = 2;
    osm->tags = malloc(osm->tag_count * sizeof(struct osm_tag));
    strcpy(osm->tags[0].key, "railway");
    strcpy(osm->tags[0].value, "*");
    strcpy(osm->tags[1].key, "route");
    strcpy(osm->tags[1].value, "train");

    /* First pass. Read all node, way and relation IDs, and IDs of
     * all ways referenced in relations. */
    fprintf(stderr, "First pass...\n");
    if( !parse_entire_file(argv[1], load_node, load_way_1, load_relation, osm))
        return 1;

    /* Relation and way lists are complete after first pass. Sort,
     * remove duplicates and resize. */
    sort_ids(osm, OSM_WAY);
    sort_ids(osm, OSM_RELATION);

    /* Second pass. Read IDs of all nodes referenced in ways. */
    fprintf(stderr, "Second pass...\n");
    if( !parse_entire_file(argv[1], NULL, load_way_2, NULL, osm))
        return 1;

    /* Node list is now complete. Sort, remove duplicates and resize. */
    sort_ids(osm, OSM_NODE);

    fprintf(stderr, "Finished loading.\nElements of interest:\nNodes:\t%d\n Ways:\t%d\n Relations:\t%d\n",
            osm->count[OSM_NODE], osm->count[OSM_WAY], osm->count[OSM_RELATION]);
   
    /* Third pass. Output all interesting nodes, ways and relations. */
    fprintf(stderr, "Third pass...\n");
    printf("<?xml version='1.0' encoding='UTF-8'?>\n");
    printf("<osm version=\"0.6\" generator=\"osmrail by Paul Kelly\">\n");
    if( !parse_entire_file(argv[1], output_node, output_way, output_relation, osm))
        return 1;
    printf("</osm>\n");

    return 0;
}

static int parse_entire_file(char *filename, osm_node_callback_t *cb_node, 
		      osm_way_callback_t *cb_way, osm_relation_callback_t *cb_relation, void *data)
{
    struct osm_planet *osf;
    struct osm_parse *parse;

    if( !(osf = osm_planet_open(filename)))
    {
        fprintf(stderr, "Unable to open file <%s>\n", filename);
        return 0;
    }

    if( !(parse = osm_parse_init(cb_node, cb_way, cb_relation, data)))
    {
        fprintf(stderr, "Unable to initialise OSM parser\n");
        return 0;
    }

    while(1)
    {
        char *recvbuff;
        int ret = osm_planet_readln(osf, &recvbuff);

        if(ret == 1) /* error */
            return 0;

        /* Stop reading when either EOF or logical end of data occurs,
         * whichever is sooner */
        if( ret == 2 /* EOF */
         || osm_parse_ingest(parse, recvbuff) == 1)
            break;
    }

    if(osm_planet_close(osf) != 0)
        return 0;
    osm_parse_destroy(parse);

    return 1;
}

static int check_tags(struct osm_tag *, int tag_count, struct osm_params *);
static void ensure_capacity(struct osm_params *, int ele_type, int count);

static int cmp_id(const void *a, const void *b)
{
    unsigned int aa = *(unsigned int *)a, bb = *(unsigned int *)b;

    return (int)aa - bb;
}

/* Callback function called by osm_parse_ingest() every time a new node has
 * been ingested from the OpenStreetMap data. */
static void load_node(struct osm_node *node, void *data)
{
    struct osm_params *osm = data;

    if( !check_tags(node->tags, node->tag_count, osm))
        return;

    ensure_capacity(osm, OSM_NODE, 1);
    osm->ids[OSM_NODE][osm->count[OSM_NODE]++] = node->id;

    return;
}

static void load_way_1(struct osm_way *way, void *data)
{
    struct osm_params *osm = data;

    if( !check_tags(way->tags, way->tag_count, osm))
        return;

    ensure_capacity(osm, OSM_WAY, 1);
    osm->ids[OSM_WAY][osm->count[OSM_WAY]++] = way->id;

    return;
}

static void load_way_2(struct osm_way *way, void *data)
{
    struct osm_params *osm = data;
    int n;

    /* Check if this is an interesting way */
    if( !bsearch(&way->id, osm->ids[OSM_WAY], osm->count[OSM_WAY], sizeof(unsigned int), cmp_id))
        return;

    ensure_capacity(osm, OSM_NODE, way->node_count);
    for(n = 0; n < way->node_count; n++)
        osm->ids[OSM_NODE][osm->count[OSM_NODE]++] = way->nodes[n];

    return;
}

static void load_relation(struct osm_relation *relation, void *data)
{
    struct osm_params *osm = data;
    int w;

    if( !check_tags(relation->tags, relation->tag_count, osm))
        return;

    ensure_capacity(osm, OSM_RELATION, 1);
    osm->ids[OSM_RELATION][osm->count[OSM_RELATION]++] = relation->id;

    ensure_capacity(osm, OSM_WAY, relation->way_count);
    for(w = 0; w < relation->way_count; w++)
        osm->ids[OSM_WAY][osm->count[OSM_WAY]++] = relation->ways[w];

    return;
}

static int check_tags(struct osm_tag *tags, int tag_count, struct osm_params *osm)
{
    int i;

    for(i = 0; i < tag_count; i++)
    {
        struct osm_tag *candidate = tags+i;
        int j;

        for(j = 0; j < osm->tag_count; j++)
        {
            struct osm_tag *wanted = osm->tags+j;

            if(wanted->key[0] == '*')
            {
                if(strcmp(wanted->value, candidate->value) == 0)
                    return 1;
            }
            else if(wanted->value[0] == '*')
            {
                if(strcmp(wanted->key, candidate->key) == 0)
                    return 1;
            }
            else if( strcmp(wanted->key, candidate->key) == 0 
                  && strcmp(wanted->value, candidate->value) == 0)
                return 1;
        }
    }

    return 0;
}

static void ensure_capacity(struct osm_params *osm, int ele, int count)
{
    if(osm->count[ele] + count > osm->max[ele])
    {
        osm->max[ele] += (10000 + count);
        osm->ids[ele] = realloc(osm->ids[ele], osm->max[ele] * sizeof(unsigned int));
    }

    return;
}

static void sort_ids(struct osm_params *osm, int ele)
{
    int curr, prev = 0;

    qsort(osm->ids[ele], osm->count[ele], sizeof(unsigned int), cmp_id);
    for(curr = 1; curr < osm->count[ele]; curr++)
    {
        if(osm->ids[ele][curr] != osm->ids[ele][prev])
            osm->ids[ele][++prev] = osm->ids[ele][curr];
    }
    osm->count[ele] = prev + 1;
    osm->ids[ele] = realloc(osm->ids[ele], osm->count[ele] * sizeof(unsigned int));

    return;
}

static void print_tags(struct osm_tag *, int tag_count);
static void print_xml(char *);

static void output_node(struct osm_node *node, void *data)
{
    struct osm_params *osm = data;

    /* Check if this is an interesting node */
    if( !bsearch(&node->id, osm->ids[OSM_NODE], osm->count[OSM_NODE], sizeof(unsigned int), cmp_id))
        return;

    printf("  <node id=\"%u\" lat=\"%.7f\" lon=\"%.7f\"", node->id, node->lat, node->lon);
    if(node->tag_count == 0)
    {
        printf("/>\n");
        return;
    }
    else
        printf(">\n");

    print_tags(node->tags, node->tag_count);
    printf("  </node>\n");

    return;
}

static void output_way(struct osm_way *way, void *data)
{
    struct osm_params *osm = data;
    int n;

    /* Check if this is an interesting way */
    if( !bsearch(&way->id, osm->ids[OSM_WAY], osm->count[OSM_WAY], sizeof(unsigned int), cmp_id))
        return;

    printf("  <way id=\"%u\">\n", way->id);

    for(n = 0; n < way->node_count; n++)
        printf("    <nd ref=\"%u\"/>\n", way->nodes[n]);

    print_tags(way->tags, way->tag_count);
    printf("  </way>\n");

    return;
}

static void output_relation(struct osm_relation *relation, void *data)
{
    struct osm_params *osm = data;
    int n, w;

    /* Check if this is an interesting relation */
    if( !bsearch(&relation->id, osm->ids[OSM_RELATION], osm->count[OSM_RELATION], sizeof(unsigned int), cmp_id))
        return;

    printf("  <relation id=\"%u\">\n", relation->id);

    for(n = 0; n < relation->node_count; n++)
    {
        printf("    <member type=\"node\" ref=\"%u\" role=\"", relation->nodes[n]);
        print_xml((char *)relation->node_roles[n]);
        printf("\"/>\n");
    }

    for(w = 0; w < relation->way_count; w++)
    {
        printf("    <member type=\"way\" ref=\"%u\" role=\"", relation->ways[w]);
        print_xml((char *)relation->way_roles[w]);
        printf("\"/>\n");
    }

    print_tags(relation->tags, relation->tag_count);
    printf("  </relation>\n");

    return;
}

static void print_tags(struct osm_tag *tags, int tag_count)
{
    int t;

    for(t = 0; t < tag_count; t++)
    {
        struct osm_tag *tag = tags+t;

        printf("    <tag k=\"");
        print_xml(tag->key);
        printf("\" v=\"");
        print_xml(tag->value);
        printf("\" />\n");
    }

    return;
}

static void print_xml(char *str)
{
    int c;

    while((c = *str++))
    {
        switch(c)
        {
            case '\'':
                printf("&apos;");
                break;
            case '"':
                printf("&quot;");
                break;
            case '<':
                printf("&lt;");
                break;
            case '>':
                printf("&gt;");
                break;
            case '&':
                if(*str != '#')
                {
                    printf("&amp;");
                    break;
                }
            default:
                putchar(c);
                break;
        }
    }

    return;
}
