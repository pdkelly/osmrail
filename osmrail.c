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

static osm_node_callback_t load_node, output_node;
static osm_way_callback_t load_way_1, load_way_2, output_way;
static osm_relation_callback_t load_relation, output_relation;

static int check_tags(struct osm_tag *, int tag_count, struct osm_params *);
static void ensure_capacity(struct osm_params *, int ele_type, int count);
static void print_tags(struct osm_tag *, int tag_count);
static void print_xml(char *);

static int cmp_id(const void *a, const void *b)
{
    unsigned int aa = *(unsigned int *)a, bb = *(unsigned int *)b;

    return (int)aa - bb;
}

int main(int argc, char **argv)
{
    struct osm_params *osm = calloc(1, sizeof(struct osm_params));
    struct osm_planet *osf;
    struct osm_parse *parse;
    int ele;

    if(argc != 2)
    {
        fprintf(stderr, "Usage: %s <planet.osm.bz2>\n", argv[0]);
        return 1;
    }

    osm->tag_count = 2;
    osm->tags = malloc(osm->tag_count * sizeof(struct osm_tag));
    strcpy(osm->tags[0].key, "railway");
    strcpy(osm->tags[0].value, "*");
    strcpy(osm->tags[1].key, "route");
    strcpy(osm->tags[1].value, "train");

    /* First pass. Read all node, way and relation IDs, and IDs of
     * all ways referenced in relations. */
    fprintf(stderr, "First pass...\n");
    if( !(osf = osm_planet_open(argv[1])))
    {
        fprintf(stderr, "Unable to open file <%s>\n", argv[1]);
        return 1;
    }

    if( !(parse = osm_parse_init(load_node, load_way_1, load_relation, osm)))
    {
        fprintf(stderr, "Unable to initialise OSM parser\n");
        return 1;
    }

    while(1)
    {
        char *recvbuff;
        int ret = osm_planet_readln(osf, &recvbuff);

        if(ret == 1) /* error */
            return -1;

        /* Stop reading when either EOF or logical end of data occurs,
         * whichever is sooner */
        if( ret == 2 /* EOF */
         || osm_parse_ingest(parse, recvbuff) == 1)
            break;
    }

    if(osm_planet_close(osf) != 0)
        return -1;
    osm_parse_destroy(parse);

    /* Relation and way lists are complete after first pass. Sort,
     * remove duplicates and resize. */
    for(ele = OSM_WAY; ele < OSM_RELATION; ele++)
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
    }

    /* Second pass. Read IDs of all nodes referenced in ways. */
    fprintf(stderr, "Second pass...\n");
    if( !(osf = osm_planet_open(argv[1])))
    {
        fprintf(stderr, "Unable to open file <%s>\n", argv[1]);
        return 1;
    }

    if( !(parse = osm_parse_init(NULL, load_way_2, NULL, osm)))
    {
        fprintf(stderr, "Unable to initialise OSM parser\n");
        return 1;
    }

    while(1)
    {
        char *recvbuff;
        int ret = osm_planet_readln(osf, &recvbuff);

        if(ret == 1) /* error */
            return -1;

        /* Stop reading when either EOF or logical end of data occurs,
         * whichever is sooner */
        if( ret == 2 /* EOF */
         || osm_parse_ingest(parse, recvbuff) == 1)
            break;
    }

    if(osm_planet_close(osf) != 0)
        return -1;
    osm_parse_destroy(parse);

    /* Node list is now complete. Sort, remove duplicates and resize. */
    {
        int curr, prev = 0;

        qsort(osm->ids[OSM_NODE], osm->count[OSM_NODE], sizeof(unsigned int), cmp_id);
        for(curr = 1; curr < osm->count[OSM_NODE]; curr++)
        {
            if(osm->ids[OSM_NODE][curr] != osm->ids[OSM_NODE][prev])
                osm->ids[OSM_NODE][++prev] = osm->ids[OSM_NODE][curr];
        }
        osm->count[OSM_NODE] = prev + 1;
        osm->ids[OSM_NODE] = realloc(osm->ids[OSM_NODE], osm->count[OSM_NODE] * sizeof(unsigned int));
    }

    fprintf(stderr, "Finished loading.\nElements of interest:\nNodes:\t%d\n Ways:\t%d\n Relations:\t%d\n",
            osm->count[OSM_NODE], osm->count[OSM_WAY], osm->count[OSM_RELATION]);

   
    /* Second pass. Read IDs of all nodes referenced in ways. */
    fprintf(stderr, "Third pass...\n");
    if( !(osf = osm_planet_open(argv[1])))
    {
        fprintf(stderr, "Unable to open file <%s>\n", argv[1]);
        return 1;
    }

    if( !(parse = osm_parse_init(output_node, output_way, output_relation, osm)))
    {
        fprintf(stderr, "Unable to initialise OSM parser\n");
        return 1;
    }

    printf("<?xml version='1.0' encoding='UTF-8'?>\n");
    printf("<osm version=\"0.6\" generator=\"osmrail by Paul Kelly\">\n");
    while(1)
    {
        char *recvbuff;
        int ret = osm_planet_readln(osf, &recvbuff);

        if(ret == 1) /* error */
            return -1;

        /* Stop reading when either EOF or logical end of data occurs,
         * whichever is sooner */
        if( ret == 2 /* EOF */
         || osm_parse_ingest(parse, recvbuff) == 1)
            break;
    }
    printf("</osm>\n");

    if(osm_planet_close(osf) != 0)
        return -1;
    osm_parse_destroy(parse);

    return 0;
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
