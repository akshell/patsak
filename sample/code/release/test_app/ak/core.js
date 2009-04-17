
// (c) 2009 by Anton Korenyushkin

/// \file core.js
/// Core ak functionality


ak.SubRel.prototype.__proto__ = ak.Query.prototype;


Date.prototype.toString = Date.prototype.toUTCString;


ak.NONE = 0;
ak.READ_ONLY   = 1 << 0;
ak.DONT_ENUM   = 1 << 1;
ak.DONT_DELETE = 1 << 2;


ak.setObjectProp(Object.prototype,
                 'setProp',
                 ak.DONT_ENUM,
                 function (name, attrs, value) {
                     return ak.setObjectProp(this, name, attrs, value);
                 });


ak.ForeignKey = function (key_fields, ref_rel, ref_fields) {
    this.key_fields = key_fields;
    this.ref_rel = ref_rel;
    this.ref_fields = ref_fields;
};


(function ()
{
    function whose()
    {
        var query = this.constructor.prototype.where.apply(this, arguments);
        if (query.length != 1)
            throw Error('whose() query got ' + query.length + ' tuples');
        return query[0];
    }


    ak.Query.prototype.setProp('whose', ak.DONT_ENUM, whose);


    function field(field_name)
    {
        if (arguments.length != 1)
            throw Error('field() requires exactly one argument');
        if (typeof(field_name) == 'object' && field_name.length)
            throw TypeError('field() argument must not be array-like');
        var query = ak.Query.prototype.only.call(this, field_name);
        var result = [];
        for (var i = 0; i < query.length; ++i)
            result.push(query[i][field_name]);
        return result;
    }


    ak.Query.prototype.setProp('field', ak.DONT_ENUM, field);


    function updateByValues(obj)
    {
        var args = [{}];
        var index = 0;
        for (var field in obj) {
            args[0][field] = '$' + (++index);
            args.push(obj[field]);
        }
        return ak.SubRel.prototype.update.apply(this, args);
    }


    ak.SubRel.prototype.setProp('updateByValues', ak.DONT_ENUM, updateByValues);


    function makeRelDelegation(func_name)
    {
        ak.Rel.prototype[func_name] = function () {
            return ak.SubRel.prototype[func_name].apply(this.all(), arguments);
        };
    }

    makeRelDelegation('where');
    makeRelDelegation('whose');
    makeRelDelegation('only');
    makeRelDelegation('by');
    makeRelDelegation('field');
    makeRelDelegation('update');
    makeRelDelegation('updateByValues');
    makeRelDelegation('delete');
})();
