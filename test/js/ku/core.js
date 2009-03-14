
// (c) 2009 by Anton Korenyushkin

/// \file core.js
/// Core ku functionality


ku.SubRel.prototype.__proto__ = ku.Query.prototype;


Date.prototype.toString = Date.prototype.toUTCString;


ku.NONE = 0;
ku.READ_ONLY   = 1 << 0;
ku.DONT_ENUM   = 1 << 1;
ku.DONT_DELETE = 1 << 2;


ku.setObjectProp(Object.prototype,
                 'setProp',
                 ku.DONT_ENUM,
                 function (name, attrs, value) {
                     return ku.setObjectProp(this, name, attrs, value);
                 });


ku.println = function () {
    var args = [];
    for (var i = 0; i < arguments.length; ++i)
        args.push(arguments[i]);
    args.push('\n');
    return ku.Ku.prototype.print.apply(this, args);
};


ku.ForeignKey = function (key_fields, ref_rel, ref_fields) {
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


    ku.Query.prototype.setProp('whose', ku.DONT_ENUM, whose);


    function field(field_name)
    {
        if (arguments.length != 1)
            throw Error('field() requires exactly one argument');
        if (typeof(field_name) == 'object' && field_name.length)
            throw TypeError('field() argument must not be array-like');
        var query = ku.Query.prototype.only.call(this, field_name);
        var result = [];
        for (var i = 0; i < query.length; ++i)
            result.push(query[i][field_name]);
        return result;
    }


    ku.Query.prototype.setProp('field', ku.DONT_ENUM, field);


    function updateByValues(obj)
    {
        var args = [{}];
        var index = 0;
        for (var field in obj) {
            args[0][field] = '$' + (++index);
            args.push(obj[field]);
        }
        return ku.SubRel.prototype.update.apply(this, args);
    }


    ku.SubRel.prototype.setProp('updateByValues', ku.DONT_ENUM, updateByValues);


    function makeRelDelegation(func_name)
    {
        ku.Rel.prototype[func_name] = function () {
            return ku.SubRel.prototype[func_name].apply(this.all(), arguments);
        }
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
