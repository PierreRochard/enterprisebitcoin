// This file was generated by ODB, object-relational mapping (ORM)
// compiler for C++.
//

#include <odb/pre.hxx>

#include "enterprise/models/blocks-odb.hxx"

#include <cassert>
#include <cstring>  // std::memcpy


#include <odb/pgsql/traits.hxx>
#include <odb/pgsql/database.hxx>
#include <odb/pgsql/transaction.hxx>
#include <odb/pgsql/connection.hxx>
#include <odb/pgsql/statement.hxx>
#include <odb/pgsql/statement-cache.hxx>
#include <odb/pgsql/simple-object-statements.hxx>
#include <odb/pgsql/container-statements.hxx>
#include <odb/pgsql/exceptions.hxx>
#include <odb/pgsql/simple-object-result.hxx>

namespace odb
{
  // eBlocks
  //

  const char access::object_traits_impl< ::eBlocks, id_pgsql >::
  persist_statement_name[] = "persist_eBlocks";

  const char access::object_traits_impl< ::eBlocks, id_pgsql >::
  find_statement_name[] = "find_eBlocks";

  const char access::object_traits_impl< ::eBlocks, id_pgsql >::
  update_statement_name[] = "update_eBlocks";

  const char access::object_traits_impl< ::eBlocks, id_pgsql >::
  erase_statement_name[] = "erase_eBlocks";

  const char access::object_traits_impl< ::eBlocks, id_pgsql >::
  query_statement_name[] = "query_eBlocks";

  const char access::object_traits_impl< ::eBlocks, id_pgsql >::
  erase_query_statement_name[] = "erase_query_eBlocks";

  const unsigned int access::object_traits_impl< ::eBlocks, id_pgsql >::
  persist_statement_types[] =
  {
    pgsql::text_oid,
    pgsql::text_oid,
    pgsql::int8_oid,
    pgsql::int8_oid,
    pgsql::int8_oid,
    pgsql::int8_oid,
    pgsql::int4_oid,
    pgsql::int4_oid,
    pgsql::int4_oid,
    pgsql::int4_oid,
    pgsql::int8_oid,
    pgsql::float8_oid,
    pgsql::text_oid,
    pgsql::text_oid,
    pgsql::text_oid,
    pgsql::text_oid,
    pgsql::int8_oid,
    pgsql::int8_oid,
    pgsql::int8_oid,
    pgsql::int8_oid,
    pgsql::int8_oid,
    pgsql::int8_oid,
    pgsql::int8_oid,
    pgsql::int8_oid
  };

  const unsigned int access::object_traits_impl< ::eBlocks, id_pgsql >::
  find_statement_types[] =
  {
    pgsql::int4_oid
  };

  const unsigned int access::object_traits_impl< ::eBlocks, id_pgsql >::
  update_statement_types[] =
  {
    pgsql::text_oid,
    pgsql::text_oid,
    pgsql::int8_oid,
    pgsql::int8_oid,
    pgsql::int8_oid,
    pgsql::int8_oid,
    pgsql::int4_oid,
    pgsql::int4_oid,
    pgsql::int4_oid,
    pgsql::int4_oid,
    pgsql::int8_oid,
    pgsql::float8_oid,
    pgsql::text_oid,
    pgsql::text_oid,
    pgsql::text_oid,
    pgsql::text_oid,
    pgsql::int8_oid,
    pgsql::int8_oid,
    pgsql::int8_oid,
    pgsql::int8_oid,
    pgsql::int8_oid,
    pgsql::int8_oid,
    pgsql::int8_oid,
    pgsql::int8_oid,
    pgsql::int4_oid
  };

  struct access::object_traits_impl< ::eBlocks, id_pgsql >::extra_statement_cache_type
  {
    extra_statement_cache_type (
      pgsql::connection&,
      image_type&,
      id_image_type&,
      pgsql::binding&,
      pgsql::binding&,
      pgsql::native_binding&,
      const unsigned int*)
    {
    }
  };

  access::object_traits_impl< ::eBlocks, id_pgsql >::id_type
  access::object_traits_impl< ::eBlocks, id_pgsql >::
  id (const id_image_type& i)
  {
    pgsql::database* db (0);
    ODB_POTENTIALLY_UNUSED (db);

    id_type id;
    {
      pgsql::value_traits<
          unsigned int,
          pgsql::id_integer >::set_value (
        id,
        i.id_value,
        i.id_null);
    }

    return id;
  }

  access::object_traits_impl< ::eBlocks, id_pgsql >::id_type
  access::object_traits_impl< ::eBlocks, id_pgsql >::
  id (const image_type& i)
  {
    pgsql::database* db (0);
    ODB_POTENTIALLY_UNUSED (db);

    id_type id;
    {
      pgsql::value_traits<
          unsigned int,
          pgsql::id_integer >::set_value (
        id,
        i.id_value,
        i.id_null);
    }

    return id;
  }

  bool access::object_traits_impl< ::eBlocks, id_pgsql >::
  grow (image_type& i,
        bool* t)
  {
    ODB_POTENTIALLY_UNUSED (i);
    ODB_POTENTIALLY_UNUSED (t);

    bool grew (false);

    // id
    //
    t[0UL] = 0;

    // hash
    //
    if (t[1UL])
    {
      i.hash_value.capacity (i.hash_size);
      grew = true;
    }

    // merkle_root
    //
    if (t[2UL])
    {
      i.merkle_root_value.capacity (i.merkle_root_size);
      grew = true;
    }

    // time
    //
    t[3UL] = 0;

    // median_time
    //
    t[4UL] = 0;

    // height
    //
    t[5UL] = 0;

    // subsidy
    //
    t[6UL] = 0;

    // transactions_count
    //
    t[7UL] = 0;

    // version
    //
    t[8UL] = 0;

    // status
    //
    t[9UL] = 0;

    // bits
    //
    t[10UL] = 0;

    // nonce
    //
    t[11UL] = 0;

    // difficulty
    //
    t[12UL] = 0;

    // chain_work
    //
    if (t[13UL])
    {
      i.chain_work_value.capacity (i.chain_work_size);
      grew = true;
    }

    // fee_data
    //
    if (t[14UL])
    {
      i.fee_data_value.capacity (i.fee_data_size);
      grew = true;
    }

    // output_data
    //
    if (t[15UL])
    {
      i.output_data_value.capacity (i.output_data_size);
      grew = true;
    }

    // input_data
    //
    if (t[16UL])
    {
      i.input_data_value.capacity (i.input_data_size);
      grew = true;
    }

    // segwit_spend_count
    //
    t[17UL] = 0;

    // outputs_count
    //
    t[18UL] = 0;

    // inputs_count
    //
    t[19UL] = 0;

    // total_output_value
    //
    t[20UL] = 0;

    // total_fees
    //
    t[21UL] = 0;

    // total_size
    //
    t[22UL] = 0;

    // total_vsize
    //
    t[23UL] = 0;

    // total_weight
    //
    t[24UL] = 0;

    return grew;
  }

  void access::object_traits_impl< ::eBlocks, id_pgsql >::
  bind (pgsql::bind* b,
        image_type& i,
        pgsql::statement_kind sk)
  {
    ODB_POTENTIALLY_UNUSED (sk);

    using namespace pgsql;

    std::size_t n (0);

    // id
    //
    if (sk != statement_insert && sk != statement_update)
    {
      b[n].type = pgsql::bind::integer;
      b[n].buffer = &i.id_value;
      b[n].is_null = &i.id_null;
      n++;
    }

    // hash
    //
    b[n].type = pgsql::bind::text;
    b[n].buffer = i.hash_value.data ();
    b[n].capacity = i.hash_value.capacity ();
    b[n].size = &i.hash_size;
    b[n].is_null = &i.hash_null;
    n++;

    // merkle_root
    //
    b[n].type = pgsql::bind::text;
    b[n].buffer = i.merkle_root_value.data ();
    b[n].capacity = i.merkle_root_value.capacity ();
    b[n].size = &i.merkle_root_size;
    b[n].is_null = &i.merkle_root_null;
    n++;

    // time
    //
    b[n].type = pgsql::bind::bigint;
    b[n].buffer = &i.time_value;
    b[n].is_null = &i.time_null;
    n++;

    // median_time
    //
    b[n].type = pgsql::bind::bigint;
    b[n].buffer = &i.median_time_value;
    b[n].is_null = &i.median_time_null;
    n++;

    // height
    //
    b[n].type = pgsql::bind::bigint;
    b[n].buffer = &i.height_value;
    b[n].is_null = &i.height_null;
    n++;

    // subsidy
    //
    b[n].type = pgsql::bind::bigint;
    b[n].buffer = &i.subsidy_value;
    b[n].is_null = &i.subsidy_null;
    n++;

    // transactions_count
    //
    b[n].type = pgsql::bind::integer;
    b[n].buffer = &i.transactions_count_value;
    b[n].is_null = &i.transactions_count_null;
    n++;

    // version
    //
    b[n].type = pgsql::bind::integer;
    b[n].buffer = &i.version_value;
    b[n].is_null = &i.version_null;
    n++;

    // status
    //
    b[n].type = pgsql::bind::integer;
    b[n].buffer = &i.status_value;
    b[n].is_null = &i.status_null;
    n++;

    // bits
    //
    b[n].type = pgsql::bind::integer;
    b[n].buffer = &i.bits_value;
    b[n].is_null = &i.bits_null;
    n++;

    // nonce
    //
    b[n].type = pgsql::bind::bigint;
    b[n].buffer = &i.nonce_value;
    b[n].is_null = &i.nonce_null;
    n++;

    // difficulty
    //
    b[n].type = pgsql::bind::double_;
    b[n].buffer = &i.difficulty_value;
    b[n].is_null = &i.difficulty_null;
    n++;

    // chain_work
    //
    b[n].type = pgsql::bind::text;
    b[n].buffer = i.chain_work_value.data ();
    b[n].capacity = i.chain_work_value.capacity ();
    b[n].size = &i.chain_work_size;
    b[n].is_null = &i.chain_work_null;
    n++;

    // fee_data
    //
    b[n].type = pgsql::bind::text;
    b[n].buffer = i.fee_data_value.data ();
    b[n].capacity = i.fee_data_value.capacity ();
    b[n].size = &i.fee_data_size;
    b[n].is_null = &i.fee_data_null;
    n++;

    // output_data
    //
    b[n].type = pgsql::bind::text;
    b[n].buffer = i.output_data_value.data ();
    b[n].capacity = i.output_data_value.capacity ();
    b[n].size = &i.output_data_size;
    b[n].is_null = &i.output_data_null;
    n++;

    // input_data
    //
    b[n].type = pgsql::bind::text;
    b[n].buffer = i.input_data_value.data ();
    b[n].capacity = i.input_data_value.capacity ();
    b[n].size = &i.input_data_size;
    b[n].is_null = &i.input_data_null;
    n++;

    // segwit_spend_count
    //
    b[n].type = pgsql::bind::bigint;
    b[n].buffer = &i.segwit_spend_count_value;
    b[n].is_null = &i.segwit_spend_count_null;
    n++;

    // outputs_count
    //
    b[n].type = pgsql::bind::bigint;
    b[n].buffer = &i.outputs_count_value;
    b[n].is_null = &i.outputs_count_null;
    n++;

    // inputs_count
    //
    b[n].type = pgsql::bind::bigint;
    b[n].buffer = &i.inputs_count_value;
    b[n].is_null = &i.inputs_count_null;
    n++;

    // total_output_value
    //
    b[n].type = pgsql::bind::bigint;
    b[n].buffer = &i.total_output_value_value;
    b[n].is_null = &i.total_output_value_null;
    n++;

    // total_fees
    //
    b[n].type = pgsql::bind::bigint;
    b[n].buffer = &i.total_fees_value;
    b[n].is_null = &i.total_fees_null;
    n++;

    // total_size
    //
    b[n].type = pgsql::bind::bigint;
    b[n].buffer = &i.total_size_value;
    b[n].is_null = &i.total_size_null;
    n++;

    // total_vsize
    //
    b[n].type = pgsql::bind::bigint;
    b[n].buffer = &i.total_vsize_value;
    b[n].is_null = &i.total_vsize_null;
    n++;

    // total_weight
    //
    b[n].type = pgsql::bind::bigint;
    b[n].buffer = &i.total_weight_value;
    b[n].is_null = &i.total_weight_null;
    n++;
  }

  void access::object_traits_impl< ::eBlocks, id_pgsql >::
  bind (pgsql::bind* b, id_image_type& i)
  {
    std::size_t n (0);
    b[n].type = pgsql::bind::integer;
    b[n].buffer = &i.id_value;
    b[n].is_null = &i.id_null;
  }

  bool access::object_traits_impl< ::eBlocks, id_pgsql >::
  init (image_type& i,
        const object_type& o,
        pgsql::statement_kind sk)
  {
    ODB_POTENTIALLY_UNUSED (i);
    ODB_POTENTIALLY_UNUSED (o);
    ODB_POTENTIALLY_UNUSED (sk);

    using namespace pgsql;

    bool grew (false);

    // hash
    //
    {
      ::std::string const& v =
        o.hash;

      bool is_null (false);
      std::size_t size (0);
      std::size_t cap (i.hash_value.capacity ());
      pgsql::value_traits<
          ::std::string,
          pgsql::id_string >::set_image (
        i.hash_value,
        size,
        is_null,
        v);
      i.hash_null = is_null;
      i.hash_size = size;
      grew = grew || (cap != i.hash_value.capacity ());
    }

    // merkle_root
    //
    {
      ::std::string const& v =
        o.merkle_root;

      bool is_null (false);
      std::size_t size (0);
      std::size_t cap (i.merkle_root_value.capacity ());
      pgsql::value_traits<
          ::std::string,
          pgsql::id_string >::set_image (
        i.merkle_root_value,
        size,
        is_null,
        v);
      i.merkle_root_null = is_null;
      i.merkle_root_size = size;
      grew = grew || (cap != i.merkle_root_value.capacity ());
    }

    // time
    //
    {
      ::int64_t const& v =
        o.time;

      bool is_null (false);
      pgsql::value_traits<
          ::int64_t,
          pgsql::id_bigint >::set_image (
        i.time_value, is_null, v);
      i.time_null = is_null;
    }

    // median_time
    //
    {
      ::int64_t const& v =
        o.median_time;

      bool is_null (false);
      pgsql::value_traits<
          ::int64_t,
          pgsql::id_bigint >::set_image (
        i.median_time_value, is_null, v);
      i.median_time_null = is_null;
    }

    // height
    //
    {
      ::int64_t const& v =
        o.height;

      bool is_null (false);
      pgsql::value_traits<
          ::int64_t,
          pgsql::id_bigint >::set_image (
        i.height_value, is_null, v);
      i.height_null = is_null;
    }

    // subsidy
    //
    {
      ::int64_t const& v =
        o.subsidy;

      bool is_null (false);
      pgsql::value_traits<
          ::int64_t,
          pgsql::id_bigint >::set_image (
        i.subsidy_value, is_null, v);
      i.subsidy_null = is_null;
    }

    // transactions_count
    //
    {
      unsigned int const& v =
        o.transactions_count;

      bool is_null (false);
      pgsql::value_traits<
          unsigned int,
          pgsql::id_integer >::set_image (
        i.transactions_count_value, is_null, v);
      i.transactions_count_null = is_null;
    }

    // version
    //
    {
      ::uint32_t const& v =
        o.version;

      bool is_null (false);
      pgsql::value_traits<
          ::uint32_t,
          pgsql::id_integer >::set_image (
        i.version_value, is_null, v);
      i.version_null = is_null;
    }

    // status
    //
    {
      ::uint32_t const& v =
        o.status;

      bool is_null (false);
      pgsql::value_traits<
          ::uint32_t,
          pgsql::id_integer >::set_image (
        i.status_value, is_null, v);
      i.status_null = is_null;
    }

    // bits
    //
    {
      ::uint32_t const& v =
        o.bits;

      bool is_null (false);
      pgsql::value_traits<
          ::uint32_t,
          pgsql::id_integer >::set_image (
        i.bits_value, is_null, v);
      i.bits_null = is_null;
    }

    // nonce
    //
    {
      ::uint64_t const& v =
        o.nonce;

      bool is_null (false);
      pgsql::value_traits<
          ::uint64_t,
          pgsql::id_bigint >::set_image (
        i.nonce_value, is_null, v);
      i.nonce_null = is_null;
    }

    // difficulty
    //
    {
      double const& v =
        o.difficulty;

      bool is_null (false);
      pgsql::value_traits<
          double,
          pgsql::id_double >::set_image (
        i.difficulty_value, is_null, v);
      i.difficulty_null = is_null;
    }

    // chain_work
    //
    {
      ::std::string const& v =
        o.chain_work;

      bool is_null (false);
      std::size_t size (0);
      std::size_t cap (i.chain_work_value.capacity ());
      pgsql::value_traits<
          ::std::string,
          pgsql::id_string >::set_image (
        i.chain_work_value,
        size,
        is_null,
        v);
      i.chain_work_null = is_null;
      i.chain_work_size = size;
      grew = grew || (cap != i.chain_work_value.capacity ());
    }

    // fee_data
    //
    {
      ::std::string const& v =
        o.fee_data;

      bool is_null (false);
      std::size_t size (0);
      std::size_t cap (i.fee_data_value.capacity ());
      pgsql::value_traits<
          ::std::string,
          pgsql::id_string >::set_image (
        i.fee_data_value,
        size,
        is_null,
        v);
      i.fee_data_null = is_null;
      i.fee_data_size = size;
      grew = grew || (cap != i.fee_data_value.capacity ());
    }

    // output_data
    //
    {
      ::std::string const& v =
        o.output_data;

      bool is_null (false);
      std::size_t size (0);
      std::size_t cap (i.output_data_value.capacity ());
      pgsql::value_traits<
          ::std::string,
          pgsql::id_string >::set_image (
        i.output_data_value,
        size,
        is_null,
        v);
      i.output_data_null = is_null;
      i.output_data_size = size;
      grew = grew || (cap != i.output_data_value.capacity ());
    }

    // input_data
    //
    {
      ::std::string const& v =
        o.input_data;

      bool is_null (false);
      std::size_t size (0);
      std::size_t cap (i.input_data_value.capacity ());
      pgsql::value_traits<
          ::std::string,
          pgsql::id_string >::set_image (
        i.input_data_value,
        size,
        is_null,
        v);
      i.input_data_null = is_null;
      i.input_data_size = size;
      grew = grew || (cap != i.input_data_value.capacity ());
    }

    // segwit_spend_count
    //
    {
      ::int64_t const& v =
        o.segwit_spend_count;

      bool is_null (false);
      pgsql::value_traits<
          ::int64_t,
          pgsql::id_bigint >::set_image (
        i.segwit_spend_count_value, is_null, v);
      i.segwit_spend_count_null = is_null;
    }

    // outputs_count
    //
    {
      ::int64_t const& v =
        o.outputs_count;

      bool is_null (false);
      pgsql::value_traits<
          ::int64_t,
          pgsql::id_bigint >::set_image (
        i.outputs_count_value, is_null, v);
      i.outputs_count_null = is_null;
    }

    // inputs_count
    //
    {
      ::int64_t const& v =
        o.inputs_count;

      bool is_null (false);
      pgsql::value_traits<
          ::int64_t,
          pgsql::id_bigint >::set_image (
        i.inputs_count_value, is_null, v);
      i.inputs_count_null = is_null;
    }

    // total_output_value
    //
    {
      ::int64_t const& v =
        o.total_output_value;

      bool is_null (false);
      pgsql::value_traits<
          ::int64_t,
          pgsql::id_bigint >::set_image (
        i.total_output_value_value, is_null, v);
      i.total_output_value_null = is_null;
    }

    // total_fees
    //
    {
      ::int64_t const& v =
        o.total_fees;

      bool is_null (false);
      pgsql::value_traits<
          ::int64_t,
          pgsql::id_bigint >::set_image (
        i.total_fees_value, is_null, v);
      i.total_fees_null = is_null;
    }

    // total_size
    //
    {
      ::int64_t const& v =
        o.total_size;

      bool is_null (false);
      pgsql::value_traits<
          ::int64_t,
          pgsql::id_bigint >::set_image (
        i.total_size_value, is_null, v);
      i.total_size_null = is_null;
    }

    // total_vsize
    //
    {
      ::int64_t const& v =
        o.total_vsize;

      bool is_null (false);
      pgsql::value_traits<
          ::int64_t,
          pgsql::id_bigint >::set_image (
        i.total_vsize_value, is_null, v);
      i.total_vsize_null = is_null;
    }

    // total_weight
    //
    {
      ::int64_t const& v =
        o.total_weight;

      bool is_null (false);
      pgsql::value_traits<
          ::int64_t,
          pgsql::id_bigint >::set_image (
        i.total_weight_value, is_null, v);
      i.total_weight_null = is_null;
    }

    return grew;
  }

  void access::object_traits_impl< ::eBlocks, id_pgsql >::
  init (object_type& o,
        const image_type& i,
        database* db)
  {
    ODB_POTENTIALLY_UNUSED (o);
    ODB_POTENTIALLY_UNUSED (i);
    ODB_POTENTIALLY_UNUSED (db);

    // id
    //
    {
      unsigned int& v =
        o.id;

      pgsql::value_traits<
          unsigned int,
          pgsql::id_integer >::set_value (
        v,
        i.id_value,
        i.id_null);
    }

    // hash
    //
    {
      ::std::string& v =
        o.hash;

      pgsql::value_traits<
          ::std::string,
          pgsql::id_string >::set_value (
        v,
        i.hash_value,
        i.hash_size,
        i.hash_null);
    }

    // merkle_root
    //
    {
      ::std::string& v =
        o.merkle_root;

      pgsql::value_traits<
          ::std::string,
          pgsql::id_string >::set_value (
        v,
        i.merkle_root_value,
        i.merkle_root_size,
        i.merkle_root_null);
    }

    // time
    //
    {
      ::int64_t& v =
        o.time;

      pgsql::value_traits<
          ::int64_t,
          pgsql::id_bigint >::set_value (
        v,
        i.time_value,
        i.time_null);
    }

    // median_time
    //
    {
      ::int64_t& v =
        o.median_time;

      pgsql::value_traits<
          ::int64_t,
          pgsql::id_bigint >::set_value (
        v,
        i.median_time_value,
        i.median_time_null);
    }

    // height
    //
    {
      ::int64_t& v =
        o.height;

      pgsql::value_traits<
          ::int64_t,
          pgsql::id_bigint >::set_value (
        v,
        i.height_value,
        i.height_null);
    }

    // subsidy
    //
    {
      ::int64_t& v =
        o.subsidy;

      pgsql::value_traits<
          ::int64_t,
          pgsql::id_bigint >::set_value (
        v,
        i.subsidy_value,
        i.subsidy_null);
    }

    // transactions_count
    //
    {
      unsigned int& v =
        o.transactions_count;

      pgsql::value_traits<
          unsigned int,
          pgsql::id_integer >::set_value (
        v,
        i.transactions_count_value,
        i.transactions_count_null);
    }

    // version
    //
    {
      ::uint32_t& v =
        o.version;

      pgsql::value_traits<
          ::uint32_t,
          pgsql::id_integer >::set_value (
        v,
        i.version_value,
        i.version_null);
    }

    // status
    //
    {
      ::uint32_t& v =
        o.status;

      pgsql::value_traits<
          ::uint32_t,
          pgsql::id_integer >::set_value (
        v,
        i.status_value,
        i.status_null);
    }

    // bits
    //
    {
      ::uint32_t& v =
        o.bits;

      pgsql::value_traits<
          ::uint32_t,
          pgsql::id_integer >::set_value (
        v,
        i.bits_value,
        i.bits_null);
    }

    // nonce
    //
    {
      ::uint64_t& v =
        o.nonce;

      pgsql::value_traits<
          ::uint64_t,
          pgsql::id_bigint >::set_value (
        v,
        i.nonce_value,
        i.nonce_null);
    }

    // difficulty
    //
    {
      double& v =
        o.difficulty;

      pgsql::value_traits<
          double,
          pgsql::id_double >::set_value (
        v,
        i.difficulty_value,
        i.difficulty_null);
    }

    // chain_work
    //
    {
      ::std::string& v =
        o.chain_work;

      pgsql::value_traits<
          ::std::string,
          pgsql::id_string >::set_value (
        v,
        i.chain_work_value,
        i.chain_work_size,
        i.chain_work_null);
    }

    // fee_data
    //
    {
      ::std::string& v =
        o.fee_data;

      pgsql::value_traits<
          ::std::string,
          pgsql::id_string >::set_value (
        v,
        i.fee_data_value,
        i.fee_data_size,
        i.fee_data_null);
    }

    // output_data
    //
    {
      ::std::string& v =
        o.output_data;

      pgsql::value_traits<
          ::std::string,
          pgsql::id_string >::set_value (
        v,
        i.output_data_value,
        i.output_data_size,
        i.output_data_null);
    }

    // input_data
    //
    {
      ::std::string& v =
        o.input_data;

      pgsql::value_traits<
          ::std::string,
          pgsql::id_string >::set_value (
        v,
        i.input_data_value,
        i.input_data_size,
        i.input_data_null);
    }

    // segwit_spend_count
    //
    {
      ::int64_t& v =
        o.segwit_spend_count;

      pgsql::value_traits<
          ::int64_t,
          pgsql::id_bigint >::set_value (
        v,
        i.segwit_spend_count_value,
        i.segwit_spend_count_null);
    }

    // outputs_count
    //
    {
      ::int64_t& v =
        o.outputs_count;

      pgsql::value_traits<
          ::int64_t,
          pgsql::id_bigint >::set_value (
        v,
        i.outputs_count_value,
        i.outputs_count_null);
    }

    // inputs_count
    //
    {
      ::int64_t& v =
        o.inputs_count;

      pgsql::value_traits<
          ::int64_t,
          pgsql::id_bigint >::set_value (
        v,
        i.inputs_count_value,
        i.inputs_count_null);
    }

    // total_output_value
    //
    {
      ::int64_t& v =
        o.total_output_value;

      pgsql::value_traits<
          ::int64_t,
          pgsql::id_bigint >::set_value (
        v,
        i.total_output_value_value,
        i.total_output_value_null);
    }

    // total_fees
    //
    {
      ::int64_t& v =
        o.total_fees;

      pgsql::value_traits<
          ::int64_t,
          pgsql::id_bigint >::set_value (
        v,
        i.total_fees_value,
        i.total_fees_null);
    }

    // total_size
    //
    {
      ::int64_t& v =
        o.total_size;

      pgsql::value_traits<
          ::int64_t,
          pgsql::id_bigint >::set_value (
        v,
        i.total_size_value,
        i.total_size_null);
    }

    // total_vsize
    //
    {
      ::int64_t& v =
        o.total_vsize;

      pgsql::value_traits<
          ::int64_t,
          pgsql::id_bigint >::set_value (
        v,
        i.total_vsize_value,
        i.total_vsize_null);
    }

    // total_weight
    //
    {
      ::int64_t& v =
        o.total_weight;

      pgsql::value_traits<
          ::int64_t,
          pgsql::id_bigint >::set_value (
        v,
        i.total_weight_value,
        i.total_weight_null);
    }
  }

  void access::object_traits_impl< ::eBlocks, id_pgsql >::
  init (id_image_type& i, const id_type& id)
  {
    {
      bool is_null (false);
      pgsql::value_traits<
          unsigned int,
          pgsql::id_integer >::set_image (
        i.id_value, is_null, id);
      i.id_null = is_null;
    }
  }

  const char access::object_traits_impl< ::eBlocks, id_pgsql >::persist_statement[] =
  "INSERT INTO \"bitcoin\".\"eBlocks\" "
  "(\"id\", "
  "\"hash\", "
  "\"merkle_root\", "
  "\"time\", "
  "\"median_time\", "
  "\"height\", "
  "\"subsidy\", "
  "\"transactions_count\", "
  "\"version\", "
  "\"status\", "
  "\"bits\", "
  "\"nonce\", "
  "\"difficulty\", "
  "\"chain_work\", "
  "\"fee_data\", "
  "\"output_data\", "
  "\"input_data\", "
  "\"segwit_spend_count\", "
  "\"outputs_count\", "
  "\"inputs_count\", "
  "\"total_output_value\", "
  "\"total_fees\", "
  "\"total_size\", "
  "\"total_vsize\", "
  "\"total_weight\") "
  "VALUES "
  "(DEFAULT, $1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15, $16, $17, $18, $19, $20, $21, $22, $23, $24) "
  "RETURNING \"id\"";

  const char access::object_traits_impl< ::eBlocks, id_pgsql >::find_statement[] =
  "SELECT "
  "\"bitcoin\".\"eBlocks\".\"id\", "
  "\"bitcoin\".\"eBlocks\".\"hash\", "
  "\"bitcoin\".\"eBlocks\".\"merkle_root\", "
  "\"bitcoin\".\"eBlocks\".\"time\", "
  "\"bitcoin\".\"eBlocks\".\"median_time\", "
  "\"bitcoin\".\"eBlocks\".\"height\", "
  "\"bitcoin\".\"eBlocks\".\"subsidy\", "
  "\"bitcoin\".\"eBlocks\".\"transactions_count\", "
  "\"bitcoin\".\"eBlocks\".\"version\", "
  "\"bitcoin\".\"eBlocks\".\"status\", "
  "\"bitcoin\".\"eBlocks\".\"bits\", "
  "\"bitcoin\".\"eBlocks\".\"nonce\", "
  "\"bitcoin\".\"eBlocks\".\"difficulty\", "
  "\"bitcoin\".\"eBlocks\".\"chain_work\", "
  "\"bitcoin\".\"eBlocks\".\"fee_data\", "
  "\"bitcoin\".\"eBlocks\".\"output_data\", "
  "\"bitcoin\".\"eBlocks\".\"input_data\", "
  "\"bitcoin\".\"eBlocks\".\"segwit_spend_count\", "
  "\"bitcoin\".\"eBlocks\".\"outputs_count\", "
  "\"bitcoin\".\"eBlocks\".\"inputs_count\", "
  "\"bitcoin\".\"eBlocks\".\"total_output_value\", "
  "\"bitcoin\".\"eBlocks\".\"total_fees\", "
  "\"bitcoin\".\"eBlocks\".\"total_size\", "
  "\"bitcoin\".\"eBlocks\".\"total_vsize\", "
  "\"bitcoin\".\"eBlocks\".\"total_weight\" "
  "FROM \"bitcoin\".\"eBlocks\" "
  "WHERE \"bitcoin\".\"eBlocks\".\"id\"=$1";

  const char access::object_traits_impl< ::eBlocks, id_pgsql >::update_statement[] =
  "UPDATE \"bitcoin\".\"eBlocks\" "
  "SET "
  "\"hash\"=$1, "
  "\"merkle_root\"=$2, "
  "\"time\"=$3, "
  "\"median_time\"=$4, "
  "\"height\"=$5, "
  "\"subsidy\"=$6, "
  "\"transactions_count\"=$7, "
  "\"version\"=$8, "
  "\"status\"=$9, "
  "\"bits\"=$10, "
  "\"nonce\"=$11, "
  "\"difficulty\"=$12, "
  "\"chain_work\"=$13, "
  "\"fee_data\"=$14, "
  "\"output_data\"=$15, "
  "\"input_data\"=$16, "
  "\"segwit_spend_count\"=$17, "
  "\"outputs_count\"=$18, "
  "\"inputs_count\"=$19, "
  "\"total_output_value\"=$20, "
  "\"total_fees\"=$21, "
  "\"total_size\"=$22, "
  "\"total_vsize\"=$23, "
  "\"total_weight\"=$24 "
  "WHERE \"id\"=$25";

  const char access::object_traits_impl< ::eBlocks, id_pgsql >::erase_statement[] =
  "DELETE FROM \"bitcoin\".\"eBlocks\" "
  "WHERE \"id\"=$1";

  const char access::object_traits_impl< ::eBlocks, id_pgsql >::query_statement[] =
  "SELECT "
  "\"bitcoin\".\"eBlocks\".\"id\", "
  "\"bitcoin\".\"eBlocks\".\"hash\", "
  "\"bitcoin\".\"eBlocks\".\"merkle_root\", "
  "\"bitcoin\".\"eBlocks\".\"time\", "
  "\"bitcoin\".\"eBlocks\".\"median_time\", "
  "\"bitcoin\".\"eBlocks\".\"height\", "
  "\"bitcoin\".\"eBlocks\".\"subsidy\", "
  "\"bitcoin\".\"eBlocks\".\"transactions_count\", "
  "\"bitcoin\".\"eBlocks\".\"version\", "
  "\"bitcoin\".\"eBlocks\".\"status\", "
  "\"bitcoin\".\"eBlocks\".\"bits\", "
  "\"bitcoin\".\"eBlocks\".\"nonce\", "
  "\"bitcoin\".\"eBlocks\".\"difficulty\", "
  "\"bitcoin\".\"eBlocks\".\"chain_work\", "
  "\"bitcoin\".\"eBlocks\".\"fee_data\", "
  "\"bitcoin\".\"eBlocks\".\"output_data\", "
  "\"bitcoin\".\"eBlocks\".\"input_data\", "
  "\"bitcoin\".\"eBlocks\".\"segwit_spend_count\", "
  "\"bitcoin\".\"eBlocks\".\"outputs_count\", "
  "\"bitcoin\".\"eBlocks\".\"inputs_count\", "
  "\"bitcoin\".\"eBlocks\".\"total_output_value\", "
  "\"bitcoin\".\"eBlocks\".\"total_fees\", "
  "\"bitcoin\".\"eBlocks\".\"total_size\", "
  "\"bitcoin\".\"eBlocks\".\"total_vsize\", "
  "\"bitcoin\".\"eBlocks\".\"total_weight\" "
  "FROM \"bitcoin\".\"eBlocks\"";

  const char access::object_traits_impl< ::eBlocks, id_pgsql >::erase_query_statement[] =
  "DELETE FROM \"bitcoin\".\"eBlocks\"";

  const char access::object_traits_impl< ::eBlocks, id_pgsql >::table_name[] =
  "\"bitcoin\".\"eBlocks\"";

  void access::object_traits_impl< ::eBlocks, id_pgsql >::
  persist (database& db, object_type& obj)
  {
    ODB_POTENTIALLY_UNUSED (db);

    using namespace pgsql;

    pgsql::connection& conn (
      pgsql::transaction::current ().connection ());
    statements_type& sts (
      conn.statement_cache ().find_object<object_type> ());

    callback (db,
              static_cast<const object_type&> (obj),
              callback_event::pre_persist);

    image_type& im (sts.image ());
    binding& imb (sts.insert_image_binding ());

    if (init (im, obj, statement_insert))
      im.version++;

    if (im.version != sts.insert_image_version () ||
        imb.version == 0)
    {
      bind (imb.bind, im, statement_insert);
      sts.insert_image_version (im.version);
      imb.version++;
    }

    {
      id_image_type& i (sts.id_image ());
      binding& b (sts.id_image_binding ());
      if (i.version != sts.id_image_version () || b.version == 0)
      {
        bind (b.bind, i);
        sts.id_image_version (i.version);
        b.version++;
      }
    }

    insert_statement& st (sts.persist_statement ());
    if (!st.execute ())
      throw object_already_persistent ();

    obj.id = id (sts.id_image ());

    callback (db,
              static_cast<const object_type&> (obj),
              callback_event::post_persist);
  }

  void access::object_traits_impl< ::eBlocks, id_pgsql >::
  update (database& db, const object_type& obj)
  {
    ODB_POTENTIALLY_UNUSED (db);

    using namespace pgsql;
    using pgsql::update_statement;

    callback (db, obj, callback_event::pre_update);

    pgsql::transaction& tr (pgsql::transaction::current ());
    pgsql::connection& conn (tr.connection ());
    statements_type& sts (
      conn.statement_cache ().find_object<object_type> ());

    const id_type& id (
      obj.id);
    id_image_type& idi (sts.id_image ());
    init (idi, id);

    image_type& im (sts.image ());
    if (init (im, obj, statement_update))
      im.version++;

    bool u (false);
    binding& imb (sts.update_image_binding ());
    if (im.version != sts.update_image_version () ||
        imb.version == 0)
    {
      bind (imb.bind, im, statement_update);
      sts.update_image_version (im.version);
      imb.version++;
      u = true;
    }

    binding& idb (sts.id_image_binding ());
    if (idi.version != sts.update_id_image_version () ||
        idb.version == 0)
    {
      if (idi.version != sts.id_image_version () ||
          idb.version == 0)
      {
        bind (idb.bind, idi);
        sts.id_image_version (idi.version);
        idb.version++;
      }

      sts.update_id_image_version (idi.version);

      if (!u)
        imb.version++;
    }

    update_statement& st (sts.update_statement ());
    if (st.execute () == 0)
      throw object_not_persistent ();

    callback (db, obj, callback_event::post_update);
    pointer_cache_traits::update (db, obj);
  }

  void access::object_traits_impl< ::eBlocks, id_pgsql >::
  erase (database& db, const id_type& id)
  {
    using namespace pgsql;

    ODB_POTENTIALLY_UNUSED (db);

    pgsql::connection& conn (
      pgsql::transaction::current ().connection ());
    statements_type& sts (
      conn.statement_cache ().find_object<object_type> ());

    id_image_type& i (sts.id_image ());
    init (i, id);

    binding& idb (sts.id_image_binding ());
    if (i.version != sts.id_image_version () || idb.version == 0)
    {
      bind (idb.bind, i);
      sts.id_image_version (i.version);
      idb.version++;
    }

    if (sts.erase_statement ().execute () != 1)
      throw object_not_persistent ();

    pointer_cache_traits::erase (db, id);
  }

  access::object_traits_impl< ::eBlocks, id_pgsql >::pointer_type
  access::object_traits_impl< ::eBlocks, id_pgsql >::
  find (database& db, const id_type& id)
  {
    using namespace pgsql;

    {
      pointer_type p (pointer_cache_traits::find (db, id));

      if (!pointer_traits::null_ptr (p))
        return p;
    }

    pgsql::connection& conn (
      pgsql::transaction::current ().connection ());
    statements_type& sts (
      conn.statement_cache ().find_object<object_type> ());

    statements_type::auto_lock l (sts);

    if (l.locked ())
    {
      if (!find_ (sts, &id))
        return pointer_type ();
    }

    pointer_type p (
      access::object_factory<object_type, pointer_type>::create ());
    pointer_traits::guard pg (p);

    pointer_cache_traits::insert_guard ig (
      pointer_cache_traits::insert (db, id, p));

    object_type& obj (pointer_traits::get_ref (p));

    if (l.locked ())
    {
      select_statement& st (sts.find_statement ());
      ODB_POTENTIALLY_UNUSED (st);

      callback (db, obj, callback_event::pre_load);
      init (obj, sts.image (), &db);
      load_ (sts, obj, false);
      sts.load_delayed (0);
      l.unlock ();
      callback (db, obj, callback_event::post_load);
      pointer_cache_traits::load (ig.position ());
    }
    else
      sts.delay_load (id, obj, ig.position ());

    ig.release ();
    pg.release ();
    return p;
  }

  bool access::object_traits_impl< ::eBlocks, id_pgsql >::
  find (database& db, const id_type& id, object_type& obj)
  {
    using namespace pgsql;

    pgsql::connection& conn (
      pgsql::transaction::current ().connection ());
    statements_type& sts (
      conn.statement_cache ().find_object<object_type> ());

    statements_type::auto_lock l (sts);

    if (!find_ (sts, &id))
      return false;

    select_statement& st (sts.find_statement ());
    ODB_POTENTIALLY_UNUSED (st);

    reference_cache_traits::position_type pos (
      reference_cache_traits::insert (db, id, obj));
    reference_cache_traits::insert_guard ig (pos);

    callback (db, obj, callback_event::pre_load);
    init (obj, sts.image (), &db);
    load_ (sts, obj, false);
    sts.load_delayed (0);
    l.unlock ();
    callback (db, obj, callback_event::post_load);
    reference_cache_traits::load (pos);
    ig.release ();
    return true;
  }

  bool access::object_traits_impl< ::eBlocks, id_pgsql >::
  reload (database& db, object_type& obj)
  {
    using namespace pgsql;

    pgsql::connection& conn (
      pgsql::transaction::current ().connection ());
    statements_type& sts (
      conn.statement_cache ().find_object<object_type> ());

    statements_type::auto_lock l (sts);

    const id_type& id  (
      obj.id);

    if (!find_ (sts, &id))
      return false;

    select_statement& st (sts.find_statement ());
    ODB_POTENTIALLY_UNUSED (st);

    callback (db, obj, callback_event::pre_load);
    init (obj, sts.image (), &db);
    load_ (sts, obj, true);
    sts.load_delayed (0);
    l.unlock ();
    callback (db, obj, callback_event::post_load);
    return true;
  }

  bool access::object_traits_impl< ::eBlocks, id_pgsql >::
  find_ (statements_type& sts,
         const id_type* id)
  {
    using namespace pgsql;

    id_image_type& i (sts.id_image ());
    init (i, *id);

    binding& idb (sts.id_image_binding ());
    if (i.version != sts.id_image_version () || idb.version == 0)
    {
      bind (idb.bind, i);
      sts.id_image_version (i.version);
      idb.version++;
    }

    image_type& im (sts.image ());
    binding& imb (sts.select_image_binding ());

    if (im.version != sts.select_image_version () ||
        imb.version == 0)
    {
      bind (imb.bind, im, statement_select);
      sts.select_image_version (im.version);
      imb.version++;
    }

    select_statement& st (sts.find_statement ());

    st.execute ();
    auto_result ar (st);
    select_statement::result r (st.fetch ());

    if (r == select_statement::truncated)
    {
      if (grow (im, sts.select_image_truncated ()))
        im.version++;

      if (im.version != sts.select_image_version ())
      {
        bind (imb.bind, im, statement_select);
        sts.select_image_version (im.version);
        imb.version++;
        st.refetch ();
      }
    }

    return r != select_statement::no_data;
  }

  result< access::object_traits_impl< ::eBlocks, id_pgsql >::object_type >
  access::object_traits_impl< ::eBlocks, id_pgsql >::
  query (database&, const query_base_type& q)
  {
    using namespace pgsql;
    using odb::details::shared;
    using odb::details::shared_ptr;

    pgsql::connection& conn (
      pgsql::transaction::current ().connection ());

    statements_type& sts (
      conn.statement_cache ().find_object<object_type> ());

    image_type& im (sts.image ());
    binding& imb (sts.select_image_binding ());

    if (im.version != sts.select_image_version () ||
        imb.version == 0)
    {
      bind (imb.bind, im, statement_select);
      sts.select_image_version (im.version);
      imb.version++;
    }

    std::string text (query_statement);
    if (!q.empty ())
    {
      text += " ";
      text += q.clause ();
    }

    q.init_parameters ();
    shared_ptr<select_statement> st (
      new (shared) select_statement (
        sts.connection (),
        query_statement_name,
        text,
        false,
        true,
        q.parameter_types (),
        q.parameter_count (),
        q.parameters_binding (),
        imb));

    st->execute ();
    st->deallocate ();

    shared_ptr< odb::object_result_impl<object_type> > r (
      new (shared) pgsql::object_result_impl<object_type> (
        q, st, sts, 0));

    return result<object_type> (r);
  }

  unsigned long long access::object_traits_impl< ::eBlocks, id_pgsql >::
  erase_query (database&, const query_base_type& q)
  {
    using namespace pgsql;

    pgsql::connection& conn (
      pgsql::transaction::current ().connection ());

    std::string text (erase_query_statement);
    if (!q.empty ())
    {
      text += ' ';
      text += q.clause ();
    }

    q.init_parameters ();
    delete_statement st (
      conn,
      erase_query_statement_name,
      text,
      q.parameter_types (),
      q.parameter_count (),
      q.parameters_binding ());

    return st.execute ();
  }
}

#include <odb/post.hxx>