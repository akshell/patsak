
// (c) 2009 by Anton Korenyushkin

/// \file mochi-kit.js
/// MochiKit library support

import('MochiKit.js');


////////////////////////////////////////////////////////////////////////////////
// Tuple to object comparison
////////////////////////////////////////////////////////////////////////////////

(function ()
{
    function checkTuples(lhs, rhs)
    {
        if (typeof(lhs) != 'object' || typeof(rhs) != 'object')
            return false;
        return lhs instanceof ku.Tuple || rhs instanceof ku.Tuple;
    }


    function compareTuples(lhs, rhs)
    {
        var field_obj = {};
        for (var field in lhs) {
            if (!(field in rhs))
                return 1;
            var comp = compare(lhs[field], rhs[field]);
            if (comp != 0)
                return comp;
            field_obj[field] = true;
        }
        for (var field in rhs)
            if (!field_obj[field])
                return -1;
        return 0;
    }


    registerComparator('TupleComparator', checkTuples, compareTuples);
})();

////////////////////////////////////////////////////////////////////////////////
// Type comparison
////////////////////////////////////////////////////////////////////////////////

(function ()
{
    function checkTypes(lhs, rhs)
    {
        return lhs instanceof ku.Type && rhs instanceof ku.Type;
    }


    function compareTypes(lhs, rhs)
    {
        return lhs === rhs;
    }


    registerComparator('TypeComparator', checkTypes, compareTypes);
})();

////////////////////////////////////////////////////////////////////////////////
// ForeignKey comparison
////////////////////////////////////////////////////////////////////////////////

(function ()
{
    function checkForeignKeys(lhs, rhs)
    {
        return lhs instanceof ku.ForeignKey && rhs instanceof ku.ForeignKey;
    }

    function compareForeignKeys(lhs, rhs)
    {
        return (lhs.key_fields == rhs.key_fields &&
                lhs.ref_rel == rhs.ref_rel &&
                lhs.ref_fields == rhs.ref_fields);
    }

    registerComparator('ForeignKeyComparator',
                       checkForeignKeys,
                       compareForeignKeys);
})();

////////////////////////////////////////////////////////////////////////////////
// Reprs
////////////////////////////////////////////////////////////////////////////////

registerRepr('TupleRepr',
             function (arg) {
                 return arg instanceof ku.Tuple;
             },
             function (tuple) {
                 var result = '';
                 for (var field in tuple)
                     result += field + ': ' + repr(tuple[field]) + ', ';
                 if (!result.length)
                     return '{}';
                 return '{' + result.substr(0, result.length - 2) + '}';
             });


registerRepr('ForeignKeyRepr',
             function (arg) {
                 return arg instanceof ku.ForeignKey;
             },
             function (foreign_key) {
                 return ('{' +
                         'key_fields: ' + repr(foreign_key.key_fields) + ', ' +
                         'ref_rel: ' + repr(foreign_key.ref_rel) + ', ' +
                         'ref_fields: ' + repr(foreign_key.ref_fields) +
                         '}');
             });
